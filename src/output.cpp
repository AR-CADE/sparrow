#include <algorithm>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <string>

#include <drm_fourcc.h>

#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <sparrow/nonstd/wlroots-full.hpp>

#include "core.hpp"
#include "flutter/platform/engine.hpp"
#include "flutter/platform/engine/messages/output_message.hpp"
#include "flutter/platform/task.hpp"
#include "output.hpp"
#include "renderer/renderer.hpp"
#include "surface/layer_surface.hpp"
#include "surface/popup.hpp"
#include "surface/session_lock.hpp"
#include "surface/sub_surface.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"
#include "util/handle_map.hpp"
#include "util/trace.hpp"
#include "util/udmabuf.hpp"

// Get output refresh rate in mHz
int get_output_refresh(struct wlr_output *output)
{
    if (output->current_mode != nullptr)
    {
        return output->current_mode->refresh;
    }

    return 60000; // Default to 60Hz
}

// Select the vsync output based on highest refresh rate
void sparrow_select_highest_refresh_output()
{
    const auto instance = Core::instance();
    Output *best     = nullptr;
    int best_refresh = 0;

    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (!output->wlr_output)
        {
            continue;
        }

        const int refresh = get_output_refresh(output->wlr_output);
        if (refresh > best_refresh)
        {
            best_refresh = refresh;
            best = output;
        }
    }

    instance->vsync_output = best;
    if (best != nullptr)
    {
        wlr_log(WLR_INFO, "Selected vsync output: %s (%d mHz)",
            best->wlr_output->name, best_refresh);
    } else
    {
        wlr_log(WLR_INFO, "No outputs available for vsync - Flutter frame pacing "
                          "may be affected");
    }
}

#ifdef DAMAGE_HISTORY
void sparrow_output_damage_history_reset(Output *output, int width,
    int height)
{
    if (!output)
    {
        return;
    }

    for (int i = 0; i < NUM_DAMAGE_HISTORY; i++)
    {
        pixman_region32_fini(&output->damage_history[i]);
        if ((width > 0) && (height > 0))
        {
            pixman_region32_init_rect(&output->damage_history[i], 0, 0, width,
                height);
        } else
        {
            pixman_region32_init(&output->damage_history[i]);
        }
    }

    output->damage_history_idx = 0;
}

#endif

void sparrow_damage_add_box(const struct wlr_box *box, bool is_client_damage,
    bool schedule_output_frame)
{
    const auto instance = Core::instance();
    if (!instance)
    {
        return;
    }

    bool is_main_thread = (get_tid() == instance->platform_tid);
    Output *output;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (!output->wlr_output || !output->wlr_output->enabled)
        {
            continue;
        }

        struct wlr_box local_box;
        if ((box != nullptr) && (box->width > 0) && (box->height > 0))
        {
            struct wlr_box output_box;
            wlr_output_layout_get_box(instance->output_layout, output->wlr_output,
                &output_box);

            struct wlr_box intersection;
            if (wlr_box_intersection(&intersection, &output_box, box))
            {
                local_box.x     = intersection.x - output_box.x;
                local_box.y     = intersection.y - output_box.y;
                local_box.width = intersection.width;
                local_box.height = intersection.height;

                if (output->wlr_output->scale != 1.0)
                {
                    local_box.x     = (int)round(local_box.x * output->wlr_output->scale);
                    local_box.y     = (int)round(local_box.y * output->wlr_output->scale);
                    local_box.width =
                        (int)round(local_box.width * output->wlr_output->scale);
                    local_box.height =
                        (int)round(local_box.height * output->wlr_output->scale);
                }

                if (output->wlr_output->transform != WL_OUTPUT_TRANSFORM_NORMAL)
                {
                    int tw = 0, th = 0;
                    wlr_output_transformed_resolution(output->wlr_output, &tw, &th);
                    wlr_box_transform(
                        &local_box, &local_box,
                        wlr_output_transform_invert(output->wlr_output->transform), tw,
                        th);
                }

                {
                    std::lock_guard<std::mutex> lock(output->damage_mutex);
                    if (schedule_output_frame)
                    {
                        wlr_damage_ring_add_box(&output->damage_ring, &local_box);
                    }

                    if (is_client_damage)
                    {
                        pixman_region32_union_rect(
                            &output->client_damage, &output->client_damage, local_box.x,
                            local_box.y, local_box.width, local_box.height);
                    }
                }

                if (schedule_output_frame && is_main_thread)
                {
                    wlr_output_schedule_frame(output->wlr_output);
                }
            }
        } else
        {
            local_box.x     = 0;
            local_box.y     = 0;
            local_box.width = output->wlr_output->width;
            local_box.height = output->wlr_output->height;

            {
                std::lock_guard<std::mutex> lock(output->damage_mutex);
                if (schedule_output_frame)
                {
                    wlr_damage_ring_add_box(&output->damage_ring, &local_box);
                }

                if (is_client_damage)
                {
                    pixman_region32_union_rect(
                        &output->client_damage, &output->client_damage, local_box.x,
                        local_box.y, local_box.width, local_box.height);
                }
            }

            if (schedule_output_frame && is_main_thread)
            {
                wlr_output_schedule_frame(output->wlr_output);
            }
        }
    }

    if (schedule_output_frame && !is_main_thread &&
        (instance->platform_notify_fd >= 0))
    {
        eventfd_write(instance->platform_notify_fd, 1);
    }
}

// Set a specific output as the vsync driver (0 = auto/highest)
void sparrow_set_vsync_output(uint32_t output_id)
{
    const auto instance = Core::instance();
    if ((output_id != 0) && instance->vsync_output &&
        (instance->vsync_output->id == output_id))
    {
        return;
    }

    if (output_id == 0)
    {
        sparrow_select_highest_refresh_output();
        return;
    }

    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (output->id == output_id)
        {
            instance->vsync_output = output;
            wlr_log(WLR_INFO, "Set vsync output to: %s (id=%d)",
                output->wlr_output->name, output_id);
            return;
        }
    }

    wlr_log(WLR_ERROR, "Vsync output id %d not found, using auto", output_id);
    sparrow_select_highest_refresh_output();
}

// Set vsync rate limit (0 = unlimited, >0 = max Hz for power saving)
void sparrow_set_vsync_rate_limit(int max_hz)
{
    const auto instance = Core::instance();
    if (instance->vsync_rate_limit == max_hz)
    {
        return;
    }

    instance->vsync_rate_limit = max_hz;
    wlr_log(WLR_INFO, "Vsync rate limit set to: %d Hz (0 = unlimited)", max_hz);
}

// Find output by ID
static Output *find_output_by_id(uint32_t output_id)
{
    const auto instance = Core::instance();
    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (output->id == output_id)
        {
            return output;
        }
    }
    return nullptr;
}

// Multi-monitor support: determine which output a box is on (by center point)
Output *sparrow_output_for_box(int x, int y, int width, int height)
{
    const auto instance = Core::instance();

    // Use center point to determine output
    const double cx = (double)x + (double)width / 2.0;
    const double cy = (double)y + (double)height / 2.0;

    const struct wlr_output *wlr_out =
        wlr_output_layout_output_at(instance->output_layout, cx, cy);

    if (wlr_out == nullptr)
    {
        // Fallback to first output
        return sparrow_get_first_output();
    }

    // Find our sparrow_output wrapper
    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (output->wlr_output == wlr_out)
        {
            return output;
        }
    }

    // Shouldn't happen, but fallback
    return sparrow_get_first_output();
}

static uint64_t get_monotonic_now_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + (ts.tv_nsec / 1000);
}

static void refresh_views_after_output_change(Core *instance)
{
    if (!instance)
    {
        return;
    }

    SparrowView *view = nullptr;
    wl_list_for_each(view, &instance->views_list, link)
    {
        if ((view->xdg_surface != nullptr) && view->xdg_surface->initialized &&
            (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) &&
            (view->toplevel != nullptr) && (view->maximized || view->fullscreen))
        {
            Output *out = view->current_output ? view->current_output :
                sparrow_get_first_output();
            if (out && out->wlr_output)
            {
                int eff_w = 0, eff_h = 0;
                wlr_output_effective_resolution(out->wlr_output, &eff_w, &eff_h);
                const bool is_wleird =
                    (!view->toplevel->app_id || view->toplevel->app_id[0] == '\0');
                if (is_wleird && (eff_h > eff_w))
                {
                    eff_h -= 1;
                }

                if ((view->pending_width != eff_w) || (view->pending_height != eff_h))
                {
                    view->pending_width  = eff_w;
                    view->pending_height = eff_h;
                    wlr_xdg_toplevel_set_size(view->toplevel, eff_w, eff_h);
                    wlr_xdg_surface_schedule_configure(view->xdg_surface);
                }
            }

            if (view->fullscreen)
            {
                view->fullscreen_active_since_ms = 0;
            }
        }

        if (view->xdg_surface && view->xdg_surface->surface &&
            view->xdg_surface->surface->mapped)
        {
            view->last_buffer_width  = 0;
            view->last_buffer_height = 0;
            if ((instance->engine != nullptr) && view->texture_registered)
            {
                instance->embedder_api.MarkExternalTextureFrameAvailable(
                    instance->engine, view->texture_id);
            }

            SparrowSubSurface *sub = nullptr;
            wl_list_for_each(sub, &view->subsurfaces, link)
            {
                if ((instance->engine != nullptr) && sub->texture_registered)
                {
                    instance->embedder_api.MarkExternalTextureFrameAvailable(
                        instance->engine, sub->texture_id);
                }
            }

            sparrow_view_damage_whole(view);
        }
    }
}

static void notify_single_output_state_changed(Output *sparrow_out)
{
    if (!sparrow_out || !sparrow_out->wlr_output)
    {
        return;
    }

    sparrow_out->last_state_change_us     = get_monotonic_now_us();
    sparrow_out->scanout_test_cache.valid = false;

#ifdef DAMAGE_HISTORY
    sparrow_output_damage_history_reset(sparrow_out,
        sparrow_out->wlr_output->width,
        sparrow_out->wlr_output->height);
#endif
    {
        std::lock_guard<std::mutex> lock(sparrow_out->damage_mutex);
        wlr_damage_ring_add_whole(&sparrow_out->damage_ring);
    }

    const auto instance = Core::instance();
    if (!instance)
    {
        return;
    }

    struct wlr_box total_box = {};
    wlr_output_layout_get_box(instance->output_layout, nullptr, &total_box);

    if ((total_box.width > 0) && (total_box.height > 0))
    {
        if (instance->cursor != nullptr)
        {
            double clamped_x = std::clamp(instance->cursor->x, 0.0,
                static_cast<double>(total_box.width - 1));
            double clamped_y = std::clamp(
                instance->cursor->y, 0.0, static_cast<double>(total_box.height - 1));
            if ((clamped_x != instance->cursor->x) ||
                (clamped_y != instance->cursor->y))
            {
                wlr_cursor_warp(instance->cursor, nullptr, clamped_x, clamped_y);
            }
        }

        if (instance->engine != nullptr)
        {
            FlutterWindowMetricsEvent window_metrics = {};
            window_metrics.struct_size = sizeof(FlutterWindowMetricsEvent);
            window_metrics.width  = total_box.width;
            window_metrics.height = total_box.height;
            window_metrics.pixel_ratio = 1.0;
            if (instance->embedder_api.SendWindowMetricsEvent != nullptr)
            {
                instance->embedder_api.SendWindowMetricsEvent(instance->engine,
                    &window_metrics);
            }

            send_output_changed(sparrow_out);
            refresh_views_after_output_change(instance);
            sparrow_popups_update_on_output_change(sparrow_out);
            sparrow_layer_surfaces_update_on_output_change(sparrow_out);
        }

        if (sparrow_is_session_locked())
        {
            sparrow_session_lock_configure_output(sparrow_out);
        }
    }

    sparrow_damage_add_box(nullptr);
}

// Set output mode (resolution + refresh rate)
bool sparrow_set_output_mode(uint32_t output_id, int width, int height,
    int refresh)
{
    Output *sparrow_out = find_output_by_id(output_id);
    if (sparrow_out == nullptr)
    {
        wlr_log(WLR_ERROR, "Output id %d not found", output_id);
        return false;
    }

    struct wlr_output *wlr_out = sparrow_out->wlr_output;

    // Find matching mode
    struct wlr_output_mode *mode;
    struct wlr_output_mode *best_mode = nullptr;
    wl_list_for_each(mode, &wlr_out->modes, link)
    {
        if ((mode->width == width) && (mode->height == height))
        {
            if ((refresh == 0) || (mode->refresh == refresh))
            {
                best_mode = mode;
                if (refresh != 0)
                {
                    break; // Exact match found
                }
            }
        }
    }

    if (best_mode == nullptr)
    {
        wlr_log(WLR_ERROR, "Mode %dx%d@%d not found for output %s", width, height,
            refresh, wlr_out->name);
        return false;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_mode(&state, best_mode);

    if (!wlr_output_commit_state(wlr_out, &state))
    {
        wlr_log(WLR_ERROR, "Failed to set mode for output %s", wlr_out->name);
        wlr_output_state_finish(&state);
        return false;
    }

    wlr_output_state_finish(&state);
    wlr_log(WLR_INFO, "Set output %s mode to %dx%d@%d", wlr_out->name,
        best_mode->width, best_mode->height, best_mode->refresh);
    sparrow_output_manager_update();
    notify_single_output_state_changed(sparrow_out);
    return true;
}

// Set output position in layout
bool sparrow_set_output_position(uint32_t output_id, int x, int y)
{
    const auto instance = Core::instance();
    Output *sparrow_out = find_output_by_id(output_id);
    if (sparrow_out == nullptr)
    {
        wlr_log(WLR_ERROR, "Output id %d not found", output_id);
        return false;
    }

    wlr_output_layout_add(instance->output_layout, sparrow_out->wlr_output, x, y);
    wlr_log(WLR_INFO, "Set output %s position to (%d, %d)",
        sparrow_out->wlr_output->name, x, y);
    sparrow_output_manager_update();
    notify_single_output_state_changed(sparrow_out);
    return true;
}

// Set output scale factor
bool sparrow_set_output_scale(uint32_t output_id, double scale)
{
    Output *sparrow_out = find_output_by_id(output_id);
    if (sparrow_out == nullptr)
    {
        wlr_log(WLR_ERROR, "Output id %d not found", output_id);
        return false;
    }

    struct wlr_output *wlr_out = sparrow_out->wlr_output;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_scale(&state, scale);

    if (!wlr_output_commit_state(wlr_out, &state))
    {
        wlr_log(WLR_ERROR, "Failed to set scale for output %s", wlr_out->name);
        wlr_output_state_finish(&state);
        return false;
    }

    wlr_output_state_finish(&state);
    wlr_log(WLR_INFO, "Set output %s scale to %.2f", wlr_out->name, scale);
    sparrow_output_manager_update();
    notify_single_output_state_changed(sparrow_out);
    return true;
}

// Set output transform (orientation)
bool sparrow_set_output_transform(uint32_t output_id, int transform)
{
    Output *sparrow_out = find_output_by_id(output_id);
    if (sparrow_out == nullptr)
    {
        wlr_log(WLR_ERROR, "Output id %d not found", output_id);
        return false;
    }

    struct wlr_output *wlr_out  = sparrow_out->wlr_output;
    enum wl_output_transform tr =
        static_cast<enum wl_output_transform>(transform);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_transform(&state, tr);

    if (!wlr_output_commit_state(wlr_out, &state))
    {
        wlr_log(WLR_ERROR, "Failed to set transform for output %s", wlr_out->name);
        wlr_output_state_finish(&state);
        return false;
    }

    wlr_output_state_finish(&state);

    wlr_log(WLR_INFO, "Set output %s transform to %d", wlr_out->name, transform);
    sparrow_output_manager_update();
    notify_single_output_state_changed(sparrow_out);
    return true;
}

#if 0
// Check if a backend is nested (running inside another compositor)
static bool is_nested_backend(struct wlr_backend *backend)
{
    if (backend == nullptr)
    {
        return false;
    }

    if (wlr_backend_is_wl(backend) || wlr_backend_is_x11(backend))
    {
        return true;
    }

    // For multi-backend, check each child
    if (wlr_backend_is_multi(backend))
    {
        // Multi-backend created by autocreate will have nested if WAYLAND_DISPLAY is set
        // The multi backend combines multiple backends; if any is WL/X11, we're nested
        // However, there's no easy way to iterate children, so check environment
        return getenv("WAYLAND_DISPLAY") != nullptr || getenv("DISPLAY") != nullptr;
    }

    return false;
}

#endif

static void send_presentation_iterator(struct wlr_surface *surface, int sx,
    int sy, void *data)
{
    struct wlr_output *wlr_output = static_cast<struct wlr_output*>(data);
    if ((wlr_output != nullptr) && (surface != nullptr) &&
        (surface->buffer != nullptr))
    {
        wlr_presentation_surface_textured_on_output(surface, wlr_output);
    }
}

static void send_scanout_presentation_iterator(struct wlr_surface *surface,
    int sx, int sy, void *data)
{
    struct wlr_output *wlr_output = static_cast<struct wlr_output*>(data);
    if ((wlr_output != nullptr) && (surface != nullptr) &&
        (surface->buffer != nullptr))
    {
        wlr_presentation_surface_scanned_out_on_output(surface, wlr_output);
    }
}

bool sparrow_surface_is_fully_opaque(const struct wlr_surface *surface)
{
    if (!surface || !wlr_surface_has_buffer(const_cast<struct wlr_surface*>(surface)))
    {
        return false;
    }

    const int w = surface->current.width;
    const int h = surface->current.height;
    if ((w <= 0) || (h <= 0))
    {
        return false;
    }

    pixman_box32_t full_box = {
        .x1 = 0,
        .y1 = 0,
        .x2 = w,
        .y2 = h,
    };
    return pixman_region32_contains_rectangle(
        const_cast<pixman_region32_t*>(&surface->opaque_region), &full_box) == PIXMAN_REGION_IN;
}

bool sparrow_output_has_fullscreen_opaque_view(const Output *output)
{
    const auto instance = Core::instance();
    if (!output || !output->wlr_output || !output->wlr_output->enabled ||
        sparrow_is_session_locked() || !instance)
    {
        return false;
    }

    // In Overview, Gesture swipe, or Screen Zoom, all views are scaled/moved and background is visible
    if (instance->force_render_all_views || instance->gesture_active ||
        instance->screen_zoom_active)
    {
        return false;
    }

    const int out_w = output->wlr_output->width;
    const int out_h = output->wlr_output->height;
    if ((out_w <= 0) || (out_h <= 0))
    {
        return false;
    }

    SparrowView *view = nullptr;
    wl_list_for_each(view, &instance->views_list, link)
    {
        if (!view->activated || !view->xdg_surface || !view->xdg_surface->surface ||
            !view->xdg_surface->surface->mapped)
        {
            continue;
        }

        if ((view->current_output != nullptr) && (view->current_output != output))
        {
            continue;
        }

        bool covers_screen = false;
        if (view->fullscreen)
        {
            covers_screen = true;
        } else
        {
            struct wlr_box v_box = {};
            int corner_radius    = 0;
            if (sparrow_get_view_buffer_box(instance, view, output->wlr_output, &v_box, &corner_radius))
            {
                if ((corner_radius == 0) && (v_box.x <= 0) && (v_box.y <= 0) &&
                    (v_box.width >= out_w) && (v_box.height >= out_h))
                {
                    covers_screen = true;
                }
            }
        }

        if (!covers_screen)
        {
            continue;
        }

        bool is_opaque = sparrow_surface_is_fully_opaque(view->xdg_surface->surface);
        if (!is_opaque)
        {
            SparrowSubSurface *sub = nullptr;
            wl_list_for_each(sub, &view->subsurfaces, link)
            {
                if (sub->surface && sub->surface->mapped && (sub->x == 0) && (sub->y == 0) &&
                    sparrow_surface_is_fully_opaque(sub->surface))
                {
                    is_opaque = true;
                    break;
                }
            }
        }

        if (is_opaque)
        {
            return true;
        }
    }

    return false;
}

bool sparrow_output_has_fullscreen_opaque_overlay(const Output *output)
{
    const auto instance = Core::instance();
    if (!output || !output->wlr_output || !output->wlr_output->enabled || !instance)
    {
        return false;
    }

    const int out_w = output->wlr_output->width;
    const int out_h = output->wlr_output->height;
    if ((out_w <= 0) || (out_h <= 0))
    {
        return false;
    }

    SparrowLayerSurface *layer_surf = nullptr;
    wl_list_for_each(layer_surf, &instance->layer_surfaces_list, link)
    {
        if (!layer_surf->layer_surface || !layer_surf->layer_surface->surface ||
            !layer_surf->layer_surface->surface->mapped)
        {
            continue;
        }

        const uint32_t layer = layer_surf->layer_surface->current.layer;
        if ((layer != ZWLR_LAYER_SHELL_V1_LAYER_TOP) &&
            (layer != ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY))
        {
            continue;
        }

        if (layer_surf->layer_surface->output &&
            (layer_surf->layer_surface->output != output->wlr_output))
        {
            continue;
        }

        struct wlr_box ls_box = {};
        if (!sparrow_get_layer_surface_buffer_box(layer_surf, output->wlr_output, &ls_box))
        {
            continue;
        }

        if ((ls_box.x <= 0) && (ls_box.y <= 0) &&
            (ls_box.width >= out_w) && (ls_box.height >= out_h))
        {
            if (sparrow_surface_is_fully_opaque(layer_surf->layer_surface->surface))
            {
                return true;
            }
        }
    }

    return false;
}

static void send_frame_done_iterator(struct wlr_surface *surface, int sx,
    int sy, void *data)
{
    struct timespec *now = static_cast<struct timespec*>(data);
    wlr_surface_send_frame_done(surface, now);
}

static void output_send_frame_done_all(Output *output, struct timespec *now)
{
    const auto instance = Core::instance();
    if (!output || !instance)
    {
        return;
    }

    // Protect against duplicate frame callbacks in the same refresh cycle
    output->last_frame_done_cycle_seq = output->current_cycle_seq;

    // Disarm timerfd if pending
    if (output->vblank_timer_fd >= 0)
    {
        struct itimerspec its = {};
        timerfd_settime(output->vblank_timer_fd, 0, &its, nullptr);
    }

    if (sparrow_is_session_locked())
    {
        SparrowSessionLockSurface *lock_surf =
            sparrow_session_lock_surface_for_output(output);
        if (lock_surf && lock_surf->wlr_lock_surface &&
            lock_surf->wlr_lock_surface->surface &&
            lock_surf->wlr_lock_surface->surface->mapped)
        {
            wlr_surface_for_each_surface(lock_surf->wlr_lock_surface->surface,
                send_frame_done_iterator, now);
        }

        return;
    }

    const bool has_fullscreen_overlay =
        sparrow_output_has_fullscreen_opaque_overlay(output);

    if (!has_fullscreen_overlay)
    {
        SparrowView *view = nullptr;
        wl_list_for_each(view, &instance->views_list, link)
        {
            if ((view->xdg_surface != nullptr) &&
                (view->xdg_surface->surface != nullptr) &&
                view->xdg_surface->surface->mapped &&
                ((view->current_output == output) ||
                 (view->current_output == nullptr)) &&
                sparrow_view_is_visible(view))
            {
                wlr_surface_for_each_surface(view->xdg_surface->surface,
                    send_frame_done_iterator, now);
            }
        }

        if (instance->popups)
        {
            for (const auto &[handle, value] : *instance->popups)
            {
                auto *popup = static_cast<SparrowPopup*>(value);
                if ((popup != nullptr) && (popup->xdg_surface != nullptr) &&
                    (popup->xdg_surface->surface != nullptr) &&
                    popup->xdg_surface->surface->mapped &&
                    (!popup->parent_view ||
                     sparrow_view_is_visible(popup->parent_view)))
                {
                    wlr_surface_for_each_surface(popup->xdg_surface->surface,
                        send_frame_done_iterator, now);
                }
            }
        }
    }

    SparrowLayerSurface *layer_surf = nullptr;
    wl_list_for_each(layer_surf, &instance->layer_surfaces_list, link)
    {
        if ((layer_surf->layer_surface != nullptr) &&
            (layer_surf->layer_surface->surface != nullptr) &&
            layer_surf->layer_surface->surface->mapped &&
            ((layer_surf->current_output == output) ||
             (layer_surf->current_output == nullptr)))
        {
            if (sparrow_layer_surface_is_occluded(layer_surf, output))
            {
                continue;
            }

            wlr_surface_for_each_surface(layer_surf->layer_surface->surface,
                send_frame_done_iterator, now);
        }
    }
}

static bool sparrow_output_is_hw_cursor_dirty(const Output *output)
{
    if ((output == nullptr) || (output->wlr_output == nullptr))
    {
        return false;
    }

    const struct wlr_output *wlr_output = output->wlr_output;
    const struct wlr_output_cursor *hw  = wlr_output->hardware_cursor;
    const struct wlr_buffer *buf = wlr_output->cursor_front_buffer;
    const double x   = (hw != nullptr) ? hw->x : 0.0;
    const double y   = (hw != nullptr) ? hw->y : 0.0;
    const int32_t hx = (hw != nullptr) ? hw->hotspot_x : 0;
    const int32_t hy = (hw != nullptr) ? hw->hotspot_y : 0;
    const bool vis   = (hw != nullptr) ? (hw->enabled && hw->visible) : false;

    return (output->last_hw_cursor != hw) ||
           (output->last_hw_cursor_buffer != buf) ||
           (output->last_hw_cursor_x != x) ||
           (output->last_hw_cursor_y != y) ||
           (output->last_hw_cursor_hotspot_x != hx) ||
           (output->last_hw_cursor_hotspot_y != hy) ||
           (output->last_hw_cursor_visible != vis);
}

static void sparrow_output_mark_hw_cursor_committed(Output *output)
{
    if ((output == nullptr) || (output->wlr_output == nullptr))
    {
        return;
    }

    const struct wlr_output *wlr_output = output->wlr_output;
    const struct wlr_output_cursor *hw  = wlr_output->hardware_cursor;

    output->last_hw_cursor = hw;
    output->last_hw_cursor_buffer = wlr_output->cursor_front_buffer;
    output->last_hw_cursor_x = (hw != nullptr) ? hw->x : 0.0;
    output->last_hw_cursor_y = (hw != nullptr) ? hw->y : 0.0;
    output->last_hw_cursor_hotspot_x = (hw != nullptr) ? hw->hotspot_x : 0;
    output->last_hw_cursor_hotspot_y = (hw != nullptr) ? hw->hotspot_y : 0;
    output->last_hw_cursor_visible   =
        (hw != nullptr) ? (hw->enabled && hw->visible) : false;
}

static int output_vblank_timer_handler(int fd, uint32_t mask, void *data)
{
    (void)mask;
    uint64_t expirations = 0;
    ssize_t s = read(fd, &expirations, sizeof(expirations));
    (void)s;

    auto *output = static_cast<Output*>(data);
    if (!output || !output->wlr_output || !output->wlr_output->enabled)
    {
        return 0;
    }

    output->current_cycle_seq++;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    output->last_present_time_ns =
        static_cast<uint64_t>(now.tv_sec) * 1000000000ULL + now.tv_nsec;
    output_send_frame_done_all(output, &now);

    const auto instance = Core::instance();
    if (instance && (output == instance->vsync_output))
    {
        intptr_t baton = atomic_exchange(&instance->vsync_baton, 0);
        if (baton != 0)
        {
            uint64_t current_time    = instance->embedder_api.GetCurrentTime();
            uint64_t frame_target_ns = output->refresh_duration_ns;
            instance->embedder_api.OnVsync(instance->engine, baton, current_time,
                current_time + frame_target_ns);
        }
    }

    if ((output->wlr_output->hardware_cursor != nullptr) &&
        sparrow_output_is_hw_cursor_dirty(output))
    {
        wlr_output_schedule_frame(output->wlr_output);
    }

    return 0;
}

static SparrowView *sparrow_output_find_fullscreen_candidate(const Output *output)
{
    const auto instance = Core::instance();
    if (!output || !output->wlr_output || !output->wlr_output->enabled ||
        sparrow_is_session_locked())
    {
        return nullptr;
    }

    struct timespec ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    uint64_t now_ms =
        (uint64_t)ts_now.tv_sec * 1000ULL + (ts_now.tv_nsec / 1000000ULL);

    bool bypass_candidate_search = false;

    // If Overview (force_render_all_views), Gestures, Dock shown
    // (!immersive_sticky), or Screen Zoom are active, direct mode must yield to
    // Flutter compositing
    if (instance->force_render_all_views || instance->gesture_active ||
        !instance->immersive_sticky || instance->screen_zoom_active)
    {
        SparrowView *v = nullptr;
        wl_list_for_each(v, &instance->views_list, link)
        {
            v->fullscreen_active_since_ms = 0;
        }
        bypass_candidate_search = true;
    }

    // If popups or menus are active, direct mode must yield to Flutter
    // compositing
    if (!bypass_candidate_search && instance->popups &&
        !instance->popups->empty())
    {
        for (const auto &[handle, value] : *instance->popups)
        {
            const auto *popup = static_cast<SparrowPopup*>(value);
            if (popup && popup->xdg_surface && popup->xdg_surface->surface &&
                popup->xdg_surface->surface->mapped)
            {
                SparrowView *v = nullptr;
                wl_list_for_each(v, &instance->views_list, link)
                {
                    v->fullscreen_active_since_ms = 0;
                }
                bypass_candidate_search = true;
                break;
            }
        }
    }

    SparrowView *candidate = nullptr;
    if (!bypass_candidate_search)
    {
        SparrowView *view = nullptr;
        wl_list_for_each(view, &instance->views_list, link)
        {
            // Only an ACTIVATED, FULLSCREEN window can be a direct candidate.
            // Background fullscreen windows on other carousel pages must never be
            // scanned out over the active page.
            if (!view->fullscreen || !view->activated)
            {
                view->fullscreen_active_since_ms = 0;
                continue;
            }

            if (!view->xdg_surface || !view->xdg_surface->surface)
            {
                continue;
            }

            if (!view->xdg_surface->surface->mapped)
            {
                continue;
            }

            if ((view->current_output != nullptr) &&
                (view->current_output != output))
            {
                continue;
            }

            // Fixed-size windows must remain in Flutter compositing mode.
            // Child views with parents are only allowed if they are fullscreen.
            if (view->is_fixed_size() || (!view->fullscreen && (view->get_parent_view() != nullptr)))
            {
                view->fullscreen_active_since_ms = 0;
                continue;
            }

            if (view->xdg_surface && view->xdg_surface->surface)
            {
                const struct wlr_surface *surf = view->xdg_surface->surface;
                if ((surf->current.width <= 0) || (surf->current.height <= 0))
                {
                    view->fullscreen_active_since_ms = 0;
                    continue;
                }
            }

            // Settling grace period (400ms):
            // Allow Flutter PageView transitions and shell animations to finish
            // smoothly before Direct Scanout bypasses Flutter compositing.
            if (view->fullscreen_active_since_ms == 0)
            {
                view->fullscreen_active_since_ms = now_ms;
            }

            constexpr uint64_t DIRECT_SCANOUT_DELAY_MS = 400;
            if (now_ms - view->fullscreen_active_since_ms < DIRECT_SCANOUT_DELAY_MS)
            {
                // Delay still active: stay in Flutter compositing mode!
                if (instance->scanout_settle_timer != nullptr)
                {
                    uint64_t remaining_ms = DIRECT_SCANOUT_DELAY_MS -
                        (now_ms - view->fullscreen_active_since_ms);
                    wl_event_source_timer_update(instance->scanout_settle_timer,
                        (int)remaining_ms + 1);
                }

                continue;
            }

            bool has_buffer = (view->locked_buffer != nullptr) ||
                (view->xdg_surface && view->xdg_surface->surface &&
                    wlr_surface_has_buffer(view->xdg_surface->surface));
            if (!has_buffer)
            {
                SparrowSubSurface *sub = nullptr;
                wl_list_for_each(sub, &view->subsurfaces, link)
                {
                    if (sub->surface && sub->surface->mapped &&
                        ((sub->locked_buffer != nullptr) ||
                         wlr_surface_has_buffer(sub->surface)))
                    {
                        has_buffer = true;
                        break;
                    }
                }
            }

            if (has_buffer)
            {
                candidate = view;
                break;
            }
        }
    }

    static int s_last_candidate_handle = -1;
    int curr_handle = candidate ? candidate->handle : -1;
    if (curr_handle != s_last_candidate_handle)
    {
        s_last_candidate_handle = curr_handle;
        if (candidate)
        {
            const char *app = (candidate->toplevel && candidate->toplevel->app_id) ?
                candidate->toplevel->app_id :
                "app";
            wlr_log(WLR_INFO,
                "[DIRECT-SCANOUT] Activated view %d ('%s') entered Direct "
                "Scanout (DS: ON)",
                candidate->handle, app);
        } else
        {
            SparrowView *act_view = nullptr;
            SparrowView *v_iter   = nullptr;
            wl_list_for_each(v_iter, &instance->views_list, link)
            {
                if (v_iter && v_iter->activated)
                {
                    act_view = v_iter;
                    break;
                }
            }
            int act_handle = act_view ? act_view->handle : -1;
            int act_fs     = act_view ? (int)act_view->fullscreen : 0;
            uint64_t delay_rem = 0;
            if (act_view && act_view->fullscreen && act_view->activated &&
                (act_view->fullscreen_active_since_ms > 0))
            {
                if (now_ms - act_view->fullscreen_active_since_ms < 400)
                {
                    delay_rem = 400 - (now_ms - act_view->fullscreen_active_since_ms);
                }
            }

            wlr_log(
                WLR_INFO,
                "[DIRECT-SCANOUT] Direct Scanout yielded to Flutter compositing "
                "(DS: OFF) "
                "[reason: overview=%d, gesture=%d, popups=%d, immersive_sticky=%d, "
                "screen_zoom=%d, settling_delay_ms=%llu, active_view=%d(fs=%d)]",
                (int)instance->force_render_all_views.load(),
                (int)instance->gesture_active,
                (int)(instance->popups && !instance->popups->empty()),
                (int)instance->immersive_sticky, (int)instance->screen_zoom_active,
                (unsigned long long)delay_rem, act_handle, act_fs);
        }
    }

    return candidate;
}

bool sparrow_view_is_in_direct_mode(const SparrowView *view)
{
    if ((view == nullptr) || !view->fullscreen || !view->activated)
    {
        return false;
    }

    Output *output =
        view->current_output ? view->current_output : sparrow_get_first_output();
    if (output == nullptr)
    {
        return false;
    }

    return output->direct_mode_active ||
           (sparrow_output_find_fullscreen_candidate(output) == view);
}

// 5x7 bitmap font for zero-overhead debug OSD
static uint8_t get_glyph_5x7(char c, int row)
{
    if ((row < 0) || (row >= 7))
    {
        return 0;
    }

    switch (c)
    {
      case '0':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
        return g[row];
    }

      case '1':
    {
        static const uint8_t g[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
        return g[row];
    }

      case '2':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F};
        return g[row];
    }

      case '3':
    {
        static const uint8_t g[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
        return g[row];
    }

      case '4':
    {
        static const uint8_t g[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
        return g[row];
    }

      case '5':
    {
        static const uint8_t g[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
        return g[row];
    }

      case '6':
    {
        static const uint8_t g[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
        return g[row];
    }

      case '7':
    {
        static const uint8_t g[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        return g[row];
    }

      case '8':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
        return g[row];
    }

      case '9':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
        return g[row];
    }

      case '.':
    {
        static const uint8_t g[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
        return g[row];
    }

      case ':':
    {
        static const uint8_t g[7] = {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00};
        return g[row];
    }

      case '|':
    {
        static const uint8_t g[7] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        return g[row];
    }

      case '-':
    {
        static const uint8_t g[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
        return g[row];
    }

      case 'A':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        return g[row];
    }

      case 'B':
    {
        static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
        return g[row];
    }

      case 'C':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
        return g[row];
    }

      case 'D':
    {
        static const uint8_t g[7] = {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C};
        return g[row];
    }

      case 'E':
    {
        static const uint8_t g[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
        return g[row];
    }

      case 'F':
    {
        static const uint8_t g[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
        return g[row];
    }

      case 'G':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
        return g[row];
    }

      case 'H':
    {
        static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        return g[row];
    }

      case 'I':
    {
        static const uint8_t g[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
        return g[row];
    }

      case 'J':
    {
        static const uint8_t g[7] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C};
        return g[row];
    }

      case 'K':
    {
        static const uint8_t g[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
        return g[row];
    }

      case 'L':
    {
        static const uint8_t g[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
        return g[row];
    }

      case 'M':
    {
        static const uint8_t g[7] = {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11};
        return g[row];
    }

      case 'N':
    {
        static const uint8_t g[7] = {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11};
        return g[row];
    }

      case 'O':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        return g[row];
    }

      case 'P':
    {
        static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
        return g[row];
    }

      case 'Q':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
        return g[row];
    }

      case 'R':
    {
        static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
        return g[row];
    }

      case 'S':
    {
        static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E};
        return g[row];
    }

      case 'T':
    {
        static const uint8_t g[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        return g[row];
    }

      case 'U':
    {
        static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        return g[row];
    }

      case 'V':
    {
        static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
        return g[row];
    }

      case 'W':
    {
        static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
        return g[row];
    }

      case 'X':
    {
        static const uint8_t g[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
        return g[row];
    }

      case 'Y':
    {
        static const uint8_t g[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
        return g[row];
    }

      case 'Z':
    {
        static const uint8_t g[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
        return g[row];
    }

      case 'm':
    {
        static const uint8_t g[7] = {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11};
        return g[row];
    }

      case 's':
    {
        static const uint8_t g[7] = {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E};
        return g[row];
    }

      default:
        return 0;
    }
}

static void prepare_fps_osd(Output *output, int out_w, int out_h,
    int damage_rect_count, uint64_t now_us)
{
    if (!output)
    {
        return;
    }

    const auto instance = Core::instance();
    if (!instance || !instance->renderer)
    {
        return;
    }

    double client_fps = instance->get_client_fps(now_us);

    const char *buf_tag;
    if (instance->buffering_mode == Core::BUFFERING_DOUBLE)
    {
        buf_tag = " | DB";
    } else if (instance->buffering_mode == Core::BUFFERING_TRIPLE)
    {
        buf_tag = " | TB:ON";
    } else
    {
        buf_tag = output->triple_buffering_active ? " | TB:AUTO" : " | DB:AUTO";
    }

    const char *scanout_tag =
        output->direct_mode_active ? " | DS:ON" : " | DS:OFF";

    const char *input_tag =
        instance->direct_input_mode ? " | DI:ON" : " | DI:OFF";

    char text[128];
    if (client_fps > 0.0)
    {
        if (damage_rect_count > 0)
        {
            snprintf(text, sizeof(text), "%.1f FPS | %.1fms | D:%d%s%s%s", client_fps,
                output->smoothed_render_ms, damage_rect_count, buf_tag,
                scanout_tag, input_tag);
        } else
        {
            snprintf(text, sizeof(text), "%.1f FPS | %.1fms%s%s%s", client_fps,
                output->smoothed_render_ms, buf_tag, scanout_tag, input_tag);
        }
    } else
    {
        snprintf(text, sizeof(text), "0.0 FPS | 0.0ms%s%s%s", buf_tag, scanout_tag, input_tag);
    }

    if ((output == instance->vsync_output) ||
        (instance->vsync_output == nullptr))
    {
        static double last_logged_fps = -1.0;
        static uint64_t last_fps_log_time_us = 0;
        if ((now_us - last_fps_log_time_us >= 1000000ULL) ||
            ((last_logged_fps > 0.0) && (client_fps == 0.0)) ||
            ((last_logged_fps == 0.0) && (client_fps > 0.0)))
        {
            last_fps_log_time_us = now_us;
            last_logged_fps = client_fps;
            wlr_log(WLR_INFO, "[FPS] %s", text);
        }
    }

    int text_len = 0;
    for (const char *p = text; *p != '\0'; ++p)
    {
        if ((*p == ' ') || (*p == '.') || (*p == ':') || (*p == '|'))
        {
            text_len += 8;
        } else
        {
            text_len += 12;
        }
    }

    const int pad_x = 10;
    const int pad_y = 6;
    const int box_w = text_len + pad_x * 2;
    const int box_h = 14 + pad_y * 2;

    // Re-generate texture only if text changed (throttled to 10Hz / 100ms when
    // active to avoid per-frame glTexImage2D uploads) or texture is null
    const bool interval_elapsed =
        (now_us >= output->last_osd_update_us) &&
        ((now_us - output->last_osd_update_us) >= 100000ULL);
    if ((output->osd_texture == nullptr) ||
        ((strcmp(output->last_osd_text, text) != 0) &&
         (interval_elapsed || (client_fps == 0.0))))
    {
        output->last_osd_update_us = now_us;
        if (output->osd_texture != nullptr)
        {
            wlr_texture_destroy(output->osd_texture);
            output->osd_texture = nullptr;
        }

        strncpy(output->last_osd_text, text, sizeof(output->last_osd_text) - 1);

        std::vector<uint32_t> pixels(static_cast<size_t>(box_w) *
            static_cast<size_t>(box_h));
        uint32_t bg_color     = 0xD80D141F;     // Dark translucent pill
        uint32_t border_color = 0x9900CCFF; // Cyan border

        for (int y = 0; y < box_h; ++y)
        {
            for (int x = 0; x < box_w; ++x)
            {
                if ((x == 0) || (x == box_w - 1) || (y == 0) || (y == box_h - 1))
                {
                    pixels[y * box_w + x] = border_color;
                } else
                {
                    pixels[y * box_w + x] = bg_color;
                }
            }
        }

        uint32_t txt_color = 0xFF99B3CC; // Muted Grey
        if (client_fps >= 55.0)
        {
            txt_color = 0xFF33FF66; // Lime Green
        } else if (client_fps >= 20.0)
        {
            txt_color = 0xFF33CCFF; // Cyan
        } else if (client_fps > 0.0)
        {
            txt_color = 0xFFFFD933; // Yellow
        }

        int cur_x = pad_x;
        for (const char *p = text; *p != '\0'; ++p)
        {
            char c = *p;
            if (c == ' ')
            {
                cur_x += 4 * 2;
                continue;
            }

            if ((c == '.') || (c == ':'))
            {
                for (int r = 0; r < 7; ++r)
                {
                    uint8_t row_bits = get_glyph_5x7(c, r);
                    if (!row_bits)
                    {
                        continue;
                    }

                    for (int py = 0; py < 2; ++py)
                    {
                        for (int px = 0; px < 4; ++px)
                        {
                            int dx = cur_x + 1 * 2 + px;
                            int dy = pad_y + r * 2 + py;
                            if ((dx < box_w) && (dy < box_h))
                            {
                                pixels[dy * box_w + dx] = txt_color;
                            }
                        }
                    }
                }

                cur_x += 4 * 2;
                continue;
            }

            if (c == '|')
            {
                for (int dy = pad_y; dy < pad_y + 14; ++dy)
                {
                    for (int px = 0; px < 2; ++px)
                    {
                        int dx = cur_x + 1 * 2 + px;
                        if ((dx < box_w) && (dy < box_h))
                        {
                            pixels[dy * box_w + dx] = txt_color;
                        }
                    }
                }

                cur_x += 4 * 2;
                continue;
            }

            for (int r = 0; r < 7; ++r)
            {
                uint8_t row_bits = get_glyph_5x7(c, r);
                if (!row_bits)
                {
                    continue;
                }

                for (int col = 0; col < 5; ++col)
                {
                    if (row_bits & (1 << (4 - col)))
                    {
                        for (int py = 0; py < 2; ++py)
                        {
                            for (int px = 0; px < 2; ++px)
                            {
                                int dx = cur_x + col * 2 + px;
                                int dy = pad_y + r * 2 + py;
                                if ((dx < box_w) && (dy < box_h))
                                {
                                    pixels[dy * box_w + dx] = txt_color;
                                }
                            }
                        }
                    }
                }
            }

            cur_x += (5 + 1) * 2;
        }

        output->osd_texture =
            wlr_texture_from_pixels(instance->renderer, DRM_FORMAT_ARGB8888,
                box_w * 4, box_w, box_h, pixels.data());
    }
}

static struct wlr_box get_fps_osd_buffer_box(Output *output)
{
    if (!output || !output->wlr_output)
    {
        return {0, 0, 0, 0};
    }

    int box_w = output->osd_texture ? output->osd_texture->width : 420;
    int box_h = output->osd_texture ? output->osd_texture->height : 60;

    int trans_w = 0, trans_h = 0;
    wlr_output_transformed_resolution(output->wlr_output, &trans_w, &trans_h);

    struct wlr_box osd_box = {
        .x     = trans_w - box_w - 16,
        .y     = 16,
        .width = box_w,
        .height = box_h,
    };

    if (output->wlr_output->transform != WL_OUTPUT_TRANSFORM_NORMAL)
    {
        wlr_box_transform(
            &osd_box, &osd_box,
            wlr_output_transform_invert(output->wlr_output->transform), trans_w,
            trans_h);
    }

    return osd_box;
}

static void render_fps_osd(struct wlr_render_pass *render_pass,
    Output *output)
{
    if (!output || !render_pass || !output->osd_texture || !output->wlr_output)
    {
        return;
    }

    struct wlr_box osd_box = get_fps_osd_buffer_box(output);

    struct wlr_render_texture_options opts = {
        .texture   = output->osd_texture,
        .dst_box   = osd_box,
        .transform = output->wlr_output->transform,
    };
    wlr_render_pass_add_texture(render_pass, &opts);
}

static void render_damage_rainbow(struct wlr_render_pass *render_pass,
    const pixman_region32_t *damage_region)
{
    if (!render_pass || !damage_region ||
        !pixman_region32_not_empty(damage_region))
    {
        return;
    }

    SPARROW_GL_SCOPE("Sparrow::DamageDebugRainbow");
    static size_t s_palette_index     = 0;
    static const float s_palette[][3] = {
        {0.00f, 0.90f, 1.00f}, // Cyan
        {1.00f, 0.15f, 0.50f}, // Neon Rose / Pink
        {0.15f, 1.00f, 0.30f}, // Lime Green
        {1.00f, 0.55f, 0.00f}, // Bright Orange
        {0.70f, 0.20f, 1.00f}, // Purple
        {1.00f, 0.90f, 0.00f}, // Yellow
        {0.00f, 0.55f, 1.00f}, // Azure Blue
        {1.00f, 0.20f, 0.10f}, // Coral Red
        {0.00f, 1.00f, 0.80f}, // Turquoise
        {1.00f, 0.00f, 0.85f}, // Magenta
        {0.45f, 1.00f, 0.00f}, // Chartreuse
        {0.20f, 0.80f, 1.00f}, // Sky Blue
    };
    static const size_t s_num_colors = sizeof(s_palette) / sizeof(s_palette[0]);

    int nrects = 0;
    const pixman_box32_t *rects =
        pixman_region32_rectangles(damage_region, &nrects);

    const float *c = s_palette[s_palette_index % s_num_colors];
    s_palette_index++;

    for (int i = 0; i < nrects; i++)
    {
        int rx = rects[i].x1;
        int ry = rects[i].y1;
        int rw = rects[i].x2 - rects[i].x1;
        int rh = rects[i].y2 - rects[i].y1;
        if ((rw <= 0) || (rh <= 0))
        {
            continue;
        }

        const int bw =
            (rw >= 24 && rh >= 24) ? 4 : ((rw >= 12 && rh >= 12) ? 3 : 2);

        struct wlr_render_rect_options top_border = {
            .box   = {.x = rx, .y = ry, .width = rw, .height = bw},
            .color = {c[0], c[1], c[2], 1.0f},
        };
        struct wlr_render_rect_options bottom_border = {
            .box   = {.x = rx, .y = ry + rh - bw, .width = rw, .height = bw},
            .color = {c[0], c[1], c[2], 1.0f},
        };
        struct wlr_render_rect_options left_border = {
            .box   = {.x = rx, .y = ry, .width = bw, .height = rh},
            .color = {c[0], c[1], c[2], 1.0f},
        };
        struct wlr_render_rect_options right_border = {
            .box   = {.x = rx + rw - bw, .y = ry, .width = bw, .height = rh},
            .color = {c[0], c[1], c[2], 1.0f},
        };
        wlr_render_pass_add_rect(render_pass, &top_border);
        wlr_render_pass_add_rect(render_pass, &bottom_border);
        wlr_render_pass_add_rect(render_pass, &left_border);
        wlr_render_pass_add_rect(render_pass, &right_border);
    }
}

struct LockSurfaceRenderData
{
    struct wlr_render_pass *render_pass = nullptr;
    Output *output = nullptr;
    struct wlr_surface *root_surface = nullptr;
};

static void render_lock_surface_iterator(struct wlr_surface *surface, int sx,
    int sy, void *data)
{
    auto *rdata = static_cast<LockSurfaceRenderData*>(data);
    if (!surface || !rdata || !rdata->render_pass || !rdata->output ||
        !rdata->output->wlr_output)
    {
        return;
    }

    struct wlr_output *wlr_output = rdata->output->wlr_output;
    struct wlr_texture *tex = wlr_surface_get_texture(surface);
    if (tex == nullptr)
    {
        tex = sparrow_surface_get_texture(surface);
    }

    if (tex == nullptr)
    {
        return;
    }

    struct wlr_box dst_box;
    if (surface == rdata->root_surface)
    {
        int eff_w = 0, eff_h = 0;
        wlr_output_effective_resolution(wlr_output, &eff_w, &eff_h);

        const bool orientation_mismatch =
            (eff_w > 0 && eff_h > 0 && surface->current.width > 0 &&
                surface->current.height > 0) &&
            ((eff_w > eff_h) != (surface->current.width > surface->current.height));

        if (orientation_mismatch)
        {
            int trans_width  = wlr_output->width;
            int trans_height = wlr_output->height;
            wlr_output_transform_coords(wlr_output->transform, &trans_width,
                &trans_height);

            double s_fit =
                std::min(static_cast<double>(eff_w) / surface->current.width,
                    static_cast<double>(eff_h) / surface->current.height);
            int fit_w    = static_cast<int>(round(surface->current.width * s_fit));
            int fit_h    = static_cast<int>(round(surface->current.height * s_fit));
            int offset_x = (eff_w - fit_w) / 2;
            int offset_y = (eff_h - fit_h) / 2;

            struct wlr_box box = {
                .x     = static_cast<int>(round(offset_x * wlr_output->scale)),
                .y     = static_cast<int>(round(offset_y * wlr_output->scale)),
                .width = static_cast<int>(round(fit_w * wlr_output->scale)),
                .height = static_cast<int>(round(fit_h * wlr_output->scale)),
            };
            enum wl_output_transform transform =
                wlr_output_transform_invert(wlr_output->transform);
            wlr_box_transform(&dst_box, &box, transform, trans_width, trans_height);
        } else
        {
            dst_box.x     = 0;
            dst_box.y     = 0;
            dst_box.width = wlr_output->width;
            dst_box.height = wlr_output->height;
        }
    } else
    {
        int trans_width  = wlr_output->width;
        int trans_height = wlr_output->height;
        wlr_output_transform_coords(wlr_output->transform, &trans_width,
            &trans_height);

        struct wlr_box box = {
            .x     = (int)round(sx * wlr_output->scale),
            .y     = (int)round(sy * wlr_output->scale),
            .width = (int)round(surface->current.width * wlr_output->scale),
            .height = (int)round(surface->current.height * wlr_output->scale),
        };
        enum wl_output_transform transform =
            wlr_output_transform_invert(wlr_output->transform);
        wlr_box_transform(&dst_box, &box, transform, trans_width, trans_height);
    }

    enum wl_output_transform transform =
        wlr_output_transform_invert(surface->current.transform);
    transform = wlr_output_transform_compose(transform, wlr_output->transform);

    struct wlr_render_texture_options opts = {
        .texture    = tex,
        .dst_box    = dst_box,
        .transform  = transform,
        .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
    };

    if (surface->current.viewport.has_src)
    {
        opts.src_box = surface->current.viewport.src;
    }

    wlr_render_pass_add_texture(rdata->render_pass, &opts);
}

struct DirectSurfaceRenderData
{
    struct wlr_render_pass *render_pass = nullptr;
    Output *output = nullptr;
    struct wlr_surface *root_surface = nullptr;
    int root_eff_w    = 0;
    int root_eff_h    = 0;
    double scale_fit  = 1.0;
    double offset_x   = 0.0;
    double offset_y   = 0.0;
    bool has_rendered = false;
};

static void render_direct_surface_iterator(struct wlr_surface *surface, int sx,
    int sy, void *data)
{
    auto *rdata = static_cast<DirectSurfaceRenderData*>(data);
    if (!surface || !rdata || !rdata->render_pass || !rdata->output ||
        !rdata->output->wlr_output)
    {
        return;
    }

    struct wlr_output *wlr_output = rdata->output->wlr_output;
    struct wlr_texture *tex = wlr_surface_get_texture(surface);
    if (tex == nullptr)
    {
        tex = sparrow_surface_get_texture(surface);
    }

    if (tex == nullptr)
    {
        return;
    }

    const int surf_w = surface->current.viewport.has_dst ?
        surface->current.viewport.dst_width :
        surface->current.width;
    const int surf_h = surface->current.viewport.has_dst ?
        surface->current.viewport.dst_height :
        surface->current.height;

    if ((surf_w <= 0) || (surf_h <= 0))
    {
        return;
    }

    // Compute surface coordinates in effective output space with uniform
    // aspect-ratio preserving scaling
    const double log_x =
        rdata->offset_x + static_cast<double>(sx) * rdata->scale_fit;
    const double log_y =
        rdata->offset_y + static_cast<double>(sy) * rdata->scale_fit;
    const double log_w = static_cast<double>(surf_w) * rdata->scale_fit;
    const double log_h = static_cast<double>(surf_h) * rdata->scale_fit;

    int trans_width  = wlr_output->width;
    int trans_height = wlr_output->height;
    wlr_output_transform_coords(wlr_output->transform, &trans_width,
        &trans_height);

    struct wlr_box box = {
        .x     = static_cast<int>(round(log_x * wlr_output->scale)),
        .y     = static_cast<int>(round(log_y * wlr_output->scale)),
        .width = static_cast<int>(round(log_w * wlr_output->scale)),
        .height = static_cast<int>(round(log_h * wlr_output->scale)),
    };

    enum wl_output_transform inv_output_transform =
        wlr_output_transform_invert(wlr_output->transform);
    struct wlr_box dst_box;
    wlr_box_transform(&dst_box, &box, inv_output_transform, trans_width,
        trans_height);

    enum wl_output_transform transform =
        wlr_output_transform_invert(surface->current.transform);
    transform = wlr_output_transform_compose(transform, wlr_output->transform);

    const bool covers_root =
        (sx <= 0 && sy <= 0 &&
            (sx + surf_w) >= (rdata->root_eff_w > 0 ? rdata->root_eff_w : surf_w) &&
            (sy + surf_h) >= (rdata->root_eff_h > 0 ? rdata->root_eff_h : surf_h));

    bool is_opaque = false;
    if (pixman_region32_not_empty(&surface->opaque_region))
    {
        pixman_box32_t *ext = pixman_region32_extents(&surface->opaque_region);
        if (ext && (ext->x1 <= 0) && (ext->y1 <= 0) && (ext->x2 >= surf_w) &&
            (ext->y2 >= surf_h))
        {
            is_opaque = true;
        }
    }

    enum wlr_render_blend_mode blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
    if ((surface == rdata->root_surface) || covers_root || is_opaque)
    {
        blend_mode = WLR_RENDER_BLEND_MODE_NONE;
    }

    struct wlr_render_texture_options opts = {
        .texture     = tex,
        .dst_box     = dst_box,
        .transform   = transform,
        .filter_mode = WLR_SCALE_FILTER_BILINEAR,
        .blend_mode  = blend_mode,
    };

    if (surface->current.viewport.has_src)
    {
        opts.src_box = surface->current.viewport.src;
    }

    wlr_render_pass_add_texture(rdata->render_pass, &opts);
    rdata->has_rendered = true;
}

static void output_handle_needs_frame(struct wl_listener *listener,
    void *data)
{
    (void)data;
    Output *output = wl_container_of(listener, output, needs_frame);
    if ((output != nullptr) && (output->wlr_output != nullptr) &&
        output->wlr_output->enabled)
    {
        wlr_output_schedule_frame(output->wlr_output);
    }
}

static void output_handle_damage(struct wl_listener *listener, void *data)
{
    Output *output = wl_container_of(listener, output, damage);
    auto *event    = static_cast<struct wlr_output_event_damage*>(data);
    if ((output == nullptr) || (output->wlr_output == nullptr) ||
        !output->wlr_output->enabled || (event == nullptr) ||
        (event->damage == nullptr))
    {
        return;
    }

    struct wlr_output *wlr_out = output->wlr_output;
    int width = 0, height = 0;
    wlr_output_transformed_resolution(wlr_out, &width, &height);

    pixman_region32_t transformed_damage;
    pixman_region32_init(&transformed_damage);
    pixman_region32_copy(&transformed_damage, event->damage);
    if (wlr_out->transform != WL_OUTPUT_TRANSFORM_NORMAL)
    {
        wlr_region_transform(&transformed_damage, &transformed_damage,
            wlr_output_transform_invert(wlr_out->transform), width,
            height);
    }

    {
        std::lock_guard<std::mutex> lock(output->damage_mutex);
        wlr_damage_ring_add(&output->damage_ring, &transformed_damage);
    }
    pixman_region32_fini(&transformed_damage);

    wlr_output_schedule_frame(wlr_out);
}

static void output_frame(struct wl_listener *listener, void *data)
{
    SPARROW_TRACE_SCOPE("Output::output_frame");
#if defined (SPARROW_ENABLE_TRACE)
    static uint32_t s_frame_index = 0;
    s_frame_index++;
    SparrowTrace::instance().start_renderdoc_frame_if_requested(s_frame_index);
#endif
    Output *output = wl_container_of(listener, output, frame);
    const auto instance = Core::instance();
    if (instance == nullptr)
    {
        return;
    }

    struct wlr_output *wlr_output = output->wlr_output;
    engine_platform_task_queue_process(instance);

    struct timespec t_frame_start;
    clock_gettime(CLOCK_MONOTONIC, &t_frame_start);
    uint64_t now_us = (uint64_t)t_frame_start.tv_sec * 1000000ULL +
        (t_frame_start.tv_nsec / 1000);

    sparrow_prune_lingering_sensitive_boxes(instance, now_us);

    const bool is_locked = sparrow_is_session_locked();

    bool needs_render = false;
    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);

    pixman_region32_t vis_damage;
    pixman_region32_init(&vis_damage);

    {
        std::lock_guard<std::mutex> lock(output->damage_mutex);
        needs_render =
            pixman_region32_not_empty(&output->damage_ring.current) ||
            instance->sparrow_renderer.current_scene.needs_update;

        if (needs_render)
        {
            pixman_region32_copy(&frame_damage, &output->damage_ring.current);
            pixman_region32_clear(&output->damage_ring.current);

            pixman_region32_copy(&vis_damage, &output->client_damage);
            pixman_region32_clear(&output->client_damage);
        }
    }

    if (!needs_render)
    {
        pixman_region32_fini(&frame_damage);
        pixman_region32_fini(&vis_damage);

        if (output == instance->vsync_output)
        {
            intptr_t baton = atomic_exchange(&instance->vsync_baton, 0);
            if (baton != 0)
            {
                uint64_t current_time    = instance->embedder_api.GetCurrentTime();
                uint64_t frame_target_ns = 16600000;
                if (wlr_output->current_mode &&
                    (wlr_output->current_mode->refresh > 0))
                {
                    frame_target_ns =
                        1000000000000ULL / wlr_output->current_mode->refresh;
                }

                if (instance->vsync_rate_limit > 0)
                {
                    uint64_t limited_ns = 1000000000ULL / instance->vsync_rate_limit;
                    if (limited_ns > frame_target_ns)
                    {
                        frame_target_ns = limited_ns;
                    }
                }

                instance->embedder_api.OnVsync(instance->engine, baton, current_time,
                    current_time + frame_target_ns);
            }
        }

        // Hardware cursor update fast-path:
        // Only commit an empty output state if the hardware cursor actually moved
        // or changed buffer/visibility since the last KMS commit! Calling
        // wlr_output_schedule_frame() sets wlr_output->needs_frame = true even when
        // the cursor has NOT moved, which previously caused a bogus empty KMS
        // pageflip 1us after OnVsync that stole the entire next 16.66ms VBlank!
        const bool hw_cursor_dirty =
            (wlr_output->hardware_cursor != nullptr) &&
            (wlr_output->current_mode != nullptr) &&
            sparrow_output_is_hw_cursor_dirty(output);

        if (hw_cursor_dirty)
        {
            struct wlr_output_state cursor_state;
            wlr_output_state_init(&cursor_state);
            if (wlr_output_commit_state(wlr_output, &cursor_state))
            {
                sparrow_output_mark_hw_cursor_committed(output);
            }

            wlr_output_state_finish(&cursor_state);
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t now_ns =
            static_cast<uint64_t>(now.tv_sec) * 1000000000ULL + now.tv_nsec;

        // Target next VBlank based on actual hardware KMS presentation timestamp
        uint64_t target_vblank_ns =
            output->last_present_time_ns + output->refresh_duration_ns;
        if ((output->last_present_time_ns == 0) || (now_ns >= target_vblank_ns))
        {
            output->current_cycle_seq++;
            output->last_present_time_ns = now_ns;
            output_send_frame_done_all(output, &now);
            return;
        }

        // Wait for remaining nanoseconds until next estimated VBlank using timerfd
        uint64_t diff_ns = target_vblank_ns - now_ns;
        struct itimerspec its = {};
        its.it_value.tv_sec  = diff_ns / 1000000000ULL;
        its.it_value.tv_nsec = diff_ns % 1000000000ULL;
        if ((its.it_value.tv_sec == 0) && (its.it_value.tv_nsec < 100000))
        {
            its.it_value.tv_nsec = 100000; // minimum 0.1ms
        }

        if (output->vblank_timer_fd >= 0)
        {
            timerfd_settime(output->vblank_timer_fd, 0, &its, nullptr);
        }

        return;
    }

#ifdef DAMAGE_HISTORY
    pixman_region32_t render_damage;
    pixman_region32_init(&render_damage);

    // Union of current damage and all buffers in history
    for (int i = 0; i < NUM_DAMAGE_HISTORY; i++)
    {
        pixman_region32_union(&render_damage, &render_damage,
            &output->damage_history[i]);
    }

    pixman_region32_union(&render_damage, &render_damage, &frame_damage);

    // Record current damage in history ring
    pixman_region32_copy(&output->damage_history[output->damage_history_idx],
        &frame_damage);
    output->damage_history_idx =
        (output->damage_history_idx + 1) % NUM_DAMAGE_HISTORY;
#else
    pixman_region32_t render_damage;
    pixman_region32_init(&render_damage);
    pixman_region32_copy(&render_damage, &frame_damage);
#endif
    // frame_damage is finalized at the end of the frame after debug visualization

    // If show_fps is active, prepare texture before begin_render_pass and
    // un-scissor in render pass
    if (instance->show_fps)
    {
        int damage_count = 0;
        if (instance->debug_damage && pixman_region32_not_empty(&vis_damage))
        {
            pixman_region32_rectangles(&vis_damage, &damage_count);
        }

        prepare_fps_osd(output, wlr_output->width, wlr_output->height, damage_count,
            now_us);
        struct wlr_box osd_box = get_fps_osd_buffer_box(output);
        pixman_region32_union_rect(&render_damage, &render_damage, osd_box.x,
            osd_box.y, osd_box.width, osd_box.height);
    }

    if (is_locked)
    {
        struct wlr_output_state output_state;
        wlr_output_state_init(&output_state);
        wlr_output_state_set_damage(&output_state, &render_damage);

        struct wlr_render_pass *render_pass =
            wlr_output_begin_render_pass(wlr_output, &output_state, nullptr);
        if (render_pass != nullptr)
        {
            SparrowSessionLockSurface *lock_surf =
                sparrow_session_lock_surface_for_output(output);

            if (lock_surf && lock_surf->committed && lock_surf->wlr_lock_surface &&
                lock_surf->wlr_lock_surface->surface &&
                lock_surf->wlr_lock_surface->surface->mapped)
            {
                struct wlr_surface *surf = lock_surf->wlr_lock_surface->surface;

                // Security baseline: always clear screen with solid black behind lock
                // surface
                struct wlr_render_rect_options bg_opts = {
                    .box   = {.x = 0,
                        .y = 0,
                        .width  = wlr_output->width,
                        .height = wlr_output->height},
                    .color = {0.0f, 0.0f, 0.0f, 1.0f},
                };
                wlr_render_pass_add_rect(render_pass, &bg_opts);

                LockSurfaceRenderData rdata = {
                    .render_pass = render_pass,
                    .output = output,
                    .root_surface = surf,
                };
                wlr_surface_for_each_surface(surf, render_lock_surface_iterator,
                    &rdata);

                struct timespec ts_done;
                clock_gettime(CLOCK_MONOTONIC, &ts_done);
                output_send_frame_done_all(output, &ts_done);
                wlr_surface_for_each_surface(surf, send_presentation_iterator,
                    wlr_output);
            } else
            {
                // Blank screen securely with solid black until lock surface arrives
                struct wlr_render_rect_options opts = {
                    .box   = {.x = 0,
                        .y = 0,
                        .width  = wlr_output->width,
                        .height = wlr_output->height},
                    .color = {0.0f, 0.0f, 0.0f, 1.0f},
                };
                wlr_render_pass_add_rect(render_pass, &opts);
            }

            wlr_render_pass_submit(render_pass);
            if (wlr_output_commit_state(wlr_output, &output_state))
            {
                sparrow_output_mark_hw_cursor_committed(output);
            }
        }

        wlr_output_state_finish(&output_state);
        pixman_region32_fini(&render_damage);
        pixman_region32_fini(&frame_damage);
        pixman_region32_fini(&vis_damage);

        if (output == instance->vsync_output)
        {
            intptr_t baton = atomic_exchange(&instance->vsync_baton, 0);
            if (baton != 0)
            {
                uint64_t current_time    = instance->embedder_api.GetCurrentTime();
                uint64_t frame_target_ns = 16600000;
                if (wlr_output->current_mode &&
                    (wlr_output->current_mode->refresh > 0))
                {
                    frame_target_ns =
                        1000000000000ULL / wlr_output->current_mode->refresh;
                }

                instance->embedder_api.OnVsync(instance->engine, baton, current_time,
                    current_time + frame_target_ns);
            }
        }

        return;
    }

    // Direct Scanout / Direct Draw check for fullscreen applications / games
    SparrowView *fs_view = sparrow_output_find_fullscreen_candidate(output);
    const bool was_direct_mode = output->direct_mode_active;
    if (fs_view != nullptr)
    {
        double refresh_budget_ms =
            (wlr_output->current_mode && wlr_output->current_mode->refresh > 0) ?
            (1000000.0 / wlr_output->current_mode->refresh) :
            16.666;

        // TIER 1: True KMS Direct Scanout (Zero GPU Render / 0ms GPU latency)
        struct wlr_buffer *scanout_buffer = nullptr;
        SparrowSubSurface *candidate_sub  = nullptr;

        // First check if a single full-screen subsurface (e.g. Vulkan/OpenGL game
        // canvas inside wxGTK/GTK3 toplevel, such as Cemu Flatpak) covers the
        // window at (0,0) with matching output dimensions.
        if (wl_list_length(&fs_view->subsurfaces) == 1)
        {
            SparrowSubSurface *sub =
                wl_container_of(fs_view->subsurfaces.next, sub, link);
            if ((sub != nullptr) && (sub->surface != nullptr) &&
                sub->surface->mapped && (sub->x == 0) && (sub->y == 0))
            {
                struct wlr_buffer *sub_buf = sub->locked_buffer;
                if ((sub_buf == nullptr) && (sub->surface->buffer != nullptr))
                {
                    sub_buf = &sub->surface->buffer->base;
                }

                if ((sub_buf != nullptr) &&
                    (sub_buf->width == wlr_output->width) &&
                    (sub_buf->height == wlr_output->height))
                {
                    candidate_sub  = sub;
                    scanout_buffer = sub_buf;
                }
            }
        }

        if (scanout_buffer == nullptr)
        {
            if (fs_view->locked_buffer != nullptr)
            {
                scanout_buffer = fs_view->locked_buffer;
            } else if (fs_view->xdg_surface && fs_view->xdg_surface->surface &&
                       (fs_view->xdg_surface->surface->buffer != nullptr))
            {
                scanout_buffer = &fs_view->xdg_surface->surface->buffer->base;
            }
        }

        struct wlr_dmabuf_attributes dmabuf_attrs = {};
        bool is_dmabuf = (scanout_buffer != nullptr) &&
            wlr_buffer_get_dmabuf(scanout_buffer, &dmabuf_attrs);

        const bool subsurfaces_ok = wl_list_empty(&fs_view->subsurfaces) ||
            ((candidate_sub != nullptr) &&
                (wl_list_length(&fs_view->subsurfaces) == 1));

        bool can_attempt_scanout =
            is_dmabuf && subsurfaces_ok && !instance->show_fps &&
            !instance->debug_damage &&
            wlr_output_is_direct_scanout_allowed(wlr_output) &&
            (dmabuf_attrs.modifier != DRM_FORMAT_MOD_INVALID) &&
            ((wlr_output->hardware_cursor != nullptr) ||
                !instance->cursor_visible) &&
            (scanout_buffer->width == wlr_output->width) &&
            (scanout_buffer->height == wlr_output->height) &&
            (wlr_output->transform == WL_OUTPUT_TRANSFORM_NORMAL);

        if (can_attempt_scanout)
        {
            const struct wlr_drm_format_set *primary_formats =
                wlr_output_get_primary_formats(wlr_output, WLR_BUFFER_CAP_DMABUF);
            if (primary_formats &&
                !wlr_drm_format_set_has(primary_formats, dmabuf_attrs.format, dmabuf_attrs.modifier))
            {
                can_attempt_scanout = false;
            }
        }

        if (can_attempt_scanout)
        {
            struct wlr_output_state scanout_state;
            wlr_output_state_init(&scanout_state);
            wlr_output_state_set_buffer(&scanout_state, scanout_buffer);
            wlr_output_state_set_damage(&scanout_state, &render_damage);

            bool test_ok = false;
            if (output->scanout_test_cache.valid &&
                (output->scanout_test_cache.format == dmabuf_attrs.format) &&
                (output->scanout_test_cache.modifier == dmabuf_attrs.modifier) &&
                (output->scanout_test_cache.width == scanout_buffer->width) &&
                (output->scanout_test_cache.height == scanout_buffer->height))
            {
                test_ok = output->scanout_test_cache.result;
            } else
            {
                test_ok = wlr_output_test_state(wlr_output, &scanout_state);
                output->scanout_test_cache.format   = dmabuf_attrs.format;
                output->scanout_test_cache.modifier = dmabuf_attrs.modifier;
                output->scanout_test_cache.width    = scanout_buffer->width;
                output->scanout_test_cache.height   = scanout_buffer->height;
                output->scanout_test_cache.result   = test_ok;
                output->scanout_test_cache.valid    = true;
            }

            if (test_ok)
            {
                if (fs_view->xdg_surface && fs_view->xdg_surface->surface)
                {
                    wlr_surface_for_each_surface(fs_view->xdg_surface->surface,
                        send_scanout_presentation_iterator,
                        wlr_output);
                }

                bool commit_ok = wlr_output_commit_state(wlr_output, &scanout_state);
                wlr_output_state_finish(&scanout_state);

                if (commit_ok)
                {
                    sparrow_output_mark_hw_cursor_committed(output);
                    if (!output->direct_mode_active)
                    {
                        const char *app = (fs_view->toplevel && fs_view->toplevel->app_id) ?
                            fs_view->toplevel->app_id :
                            "app";
                        wlr_log(WLR_INFO,
                            "[DIRECT-SCANOUT] Output '%s': True KMS Direct Scanout "
                            "ENGAGED for '%s' (DS: ON)",
                            wlr_output->name, app);
                    }

                    output->direct_scanout_flips++;
                    output->direct_mode_active = true;
                    instance->sparrow_renderer.current_scene.needs_update = false;

                    fs_view->current_buffer_sampled = true;
                    fs_view->sampled_count++;
                    if (candidate_sub != nullptr)
                    {
                        candidate_sub->current_buffer_sampled = true;
                        candidate_sub->sampled_count++;
                    }

                    output->flip_count++;
                    output->last_flip_time_us = now_us;

                    if (instance->debug_pacing &&
                        (now_us - output->last_pacing_log_time_us >= 2000000ULL))
                    {
                        output->last_pacing_log_time_us = now_us;
                        const char *app = (fs_view->toplevel && fs_view->toplevel->app_id) ?
                            fs_view->toplevel->app_id :
                            "app";
                        wlr_log(WLR_INFO,
                            "[PACING-DIRECT-SCANOUT] Output '%s': App '%s' True KMS "
                            "Direct Scanout "
                            "Active (0 GPU render) | Flips=%lu (KMS Scanout=%lu, "
                            "1-Pass Blit=%lu, Composited=%lu)",
                            wlr_output->name, app, (unsigned long)output->flip_count,
                            (unsigned long)output->direct_scanout_flips,
                            (unsigned long)output->direct_draw_flips,
                            (unsigned long)output->composited_flips);
                    }

                    if (output == instance->vsync_output)
                    {
                        intptr_t baton = atomic_exchange(&instance->vsync_baton, 0);
                        if (baton != 0)
                        {
                            uint64_t current_time    = instance->embedder_api.GetCurrentTime();
                            uint64_t frame_target_ns =
                                (uint64_t)(refresh_budget_ms * 1000000.0);
                            instance->embedder_api.OnVsync(instance->engine, baton,
                                current_time,
                                current_time + frame_target_ns);
                        }
                    }

                    struct timespec ts_done;
                    clock_gettime(CLOCK_MONOTONIC, &ts_done);
                    output_send_frame_done_all(output, &ts_done);

                    pixman_region32_fini(&render_damage);
                    pixman_region32_fini(&frame_damage);
                    pixman_region32_fini(&vis_damage);
                    return;
                } else
                {
                    output->scanout_test_cache.valid = false;
                    wlr_log(
                        WLR_ERROR,
                        "[DIRECT-SCANOUT] Output '%s': KMS Direct Scanout commit failed, aborting immediate fallback commit",
                        wlr_output->name);
                    pixman_region32_fini(&render_damage);
                    pixman_region32_fini(&frame_damage);
                    pixman_region32_fini(&vis_damage);
                    return;
                }
            } else
            {
                wlr_output_state_finish(&scanout_state);
            }
        }

        // TIER 2: Fallback to DIRECT 1-PASS GPU DRAW / BLIT (0.1ms GPU render, zero
        // latency, flicker-free with overview & gestures)
        if (!was_direct_mode)
        {
            pixman_region32_union_rect(&render_damage, &render_damage, 0, 0,
                wlr_output->width, wlr_output->height);
        }

        struct wlr_output_state direct_blit_state;
        wlr_output_state_init(&direct_blit_state);
        wlr_output_state_set_damage(&direct_blit_state, &render_damage);

        struct wlr_render_pass *direct_pass =
            wlr_output_begin_render_pass(wlr_output, &direct_blit_state, nullptr);
        if (direct_pass != nullptr)
        {
            bool has_rendered = false;

            // Clean black baseline to avoid letterbox garbage
            struct wlr_render_rect_options bg_opts = {
                .box =
                {
                    .x     = 0,
                    .y     = 0,
                    .width = wlr_output->width,
                    .height = wlr_output->height,
                },
                .color = {0.0f, 0.0f, 0.0f, 1.0f},
            };
            wlr_render_pass_add_rect(direct_pass, &bg_opts);

            // Render surface hierarchy (root + subsurfaces in proper z-order)
            struct wlr_surface *root_surf =
                (fs_view->xdg_surface) ? fs_view->xdg_surface->surface : nullptr;

            int root_w = 0;
            int root_h = 0;
            if (root_surf != nullptr)
            {
                root_w = root_surf->current.viewport.has_dst ?
                    root_surf->current.viewport.dst_width :
                    root_surf->current.width;
                root_h = root_surf->current.viewport.has_dst ?
                    root_surf->current.viewport.dst_height :
                    root_surf->current.height;

                if ((root_w <= 0) || (root_h <= 0))
                {
                    struct wlr_box extents = {.x = 0, .y = 0, .width = 0, .height = 0};
                    wlr_surface_get_extents(root_surf, &extents);
                    root_w = extents.width;
                    root_h = extents.height;
                }
            }

            int eff_w = 0, eff_h = 0;
            wlr_output_effective_resolution(wlr_output, &eff_w, &eff_h);
            if ((eff_w <= 0) || (eff_h <= 0))
            {
                eff_w = wlr_output->width;
                eff_h = wlr_output->height;
            }

            const int vis_w = (fs_view->width > 0) ? fs_view->width : root_w;
            const int vis_h = (fs_view->height > 0) ? fs_view->height : root_h;

            double s_fit = 1.0;
            double off_x = 0.0;
            double off_y = 0.0;

            if ((vis_w > 0) && (vis_h > 0))
            {
                const double out_ratio =
                    static_cast<double>(eff_w) / static_cast<double>(eff_h);
                const double surf_ratio =
                    static_cast<double>(vis_w) / static_cast<double>(vis_h);
                const bool ratio_mismatch =
                    std::abs(out_ratio - surf_ratio) / surf_ratio > 0.01;

                if (ratio_mismatch)
                {
                    s_fit = std::min(static_cast<double>(eff_w) / vis_w,
                        static_cast<double>(eff_h) / vis_h);
                    double fit_w = static_cast<double>(vis_w) * s_fit;
                    double fit_h = static_cast<double>(vis_h) * s_fit;
                    off_x = (static_cast<double>(eff_w) - fit_w) / 2.0 -
                        static_cast<double>(fs_view->geo_x) * s_fit;
                    off_y = (static_cast<double>(eff_h) - fit_h) / 2.0 -
                        static_cast<double>(fs_view->geo_y) * s_fit;
                } else
                {
                    s_fit = static_cast<double>(eff_w) / vis_w;
                    off_x = -static_cast<double>(fs_view->geo_x) * s_fit;
                    off_y = -static_cast<double>(fs_view->geo_y) * s_fit;
                }
            }

            DirectSurfaceRenderData direct_render_data = {
                .render_pass = direct_pass,
                .output = output,
                .root_surface = root_surf,
                .root_eff_w   = root_w,
                .root_eff_h   = root_h,
                .scale_fit    = s_fit,
                .offset_x     = off_x,
                .offset_y     = off_y,
                .has_rendered = false,
            };

            if (direct_render_data.root_surface != nullptr)
            {
                wlr_surface_for_each_surface(direct_render_data.root_surface,
                    render_direct_surface_iterator,
                    &direct_render_data);
            }

            has_rendered = direct_render_data.has_rendered;

            if (has_rendered)
            {
                output->direct_mode_active = true;
                instance->sparrow_renderer.current_scene.needs_update = false;

                if (instance->debug_damage && pixman_region32_not_empty(&vis_damage))
                {
                    render_damage_rainbow(direct_pass, &vis_damage);
                }

                // Render software cursor on top of fullscreen content (no-op if
                // hardware cursor plane is active)
                wlr_output_add_software_cursors_to_render_pass(wlr_output, direct_pass,
                    nullptr);

                struct timespec t_direct_now;
                clock_gettime(CLOCK_MONOTONIC, &t_direct_now);
                double cur_direct_ms =
                    ((t_direct_now.tv_sec - t_frame_start.tv_sec) * 1000.0) +
                    ((t_direct_now.tv_nsec - t_frame_start.tv_nsec) / 1000000.0);
                output->smoothed_render_ms =
                    (output->smoothed_render_ms <= 0.0) ?
                    cur_direct_ms :
                    (output->smoothed_render_ms * 0.9 + cur_direct_ms * 0.1);

                if (instance->show_fps)
                {
                    SPARROW_GL_SCOPE("Sparrow::FPS_OSD");
                    render_fps_osd(direct_pass, output);
                }

                bool submit_ok = wlr_render_pass_submit(direct_pass);
                if (submit_ok)
                {
                    if (fs_view->xdg_surface && fs_view->xdg_surface->surface)
                    {
                        wlr_surface_for_each_surface(fs_view->xdg_surface->surface,
                            send_presentation_iterator,
                            wlr_output);
                    }

                    if (wlr_output_commit_state(wlr_output, &direct_blit_state))
                    {
                        sparrow_output_mark_hw_cursor_committed(output);
                    }

                    wlr_output_state_finish(&direct_blit_state);

                    output->direct_draw_flips++;
                    if (!was_direct_mode)
                    {
                        const char *app = (fs_view->toplevel && fs_view->toplevel->app_id) ?
                            fs_view->toplevel->app_id :
                            "app";
                        wlr_log(WLR_INFO,
                            "[DIRECT-SCANOUT] Output '%s': Direct 1-Pass Blit ENGAGED for '%s' (%dx%d -> %dx%d, DS: ON)",
                            wlr_output->name, app, root_w, root_h, eff_w, eff_h);

                        // The FPS OSD of this frame was prepared before
                        // direct_mode_active flipped to true, so it still reads
                        // "DS:OFF". Idle clients won't commit again, so request
                        // one more frame to refresh the OSD with "DS:ON".
                        if (instance->show_fps)
                        {
                            sparrow_damage_add_box(nullptr, false);
                        }
                    }

                    fs_view->current_buffer_sampled = true;
                    fs_view->sampled_count++;
                    {
                        SparrowSubSurface *sub = nullptr;
                        wl_list_for_each(sub, &fs_view->subsurfaces, link)
                        {
                            if (sub->surface && sub->surface->mapped)
                            {
                                sub->current_buffer_sampled = true;
                                sub->sampled_count++;
                            }
                        }
                    }

                    output->flip_count++;
                    output->last_flip_time_us = now_us;

                    if (instance->debug_pacing &&
                        (now_us - output->last_pacing_log_time_us >= 2000000ULL))
                    {
                        output->last_pacing_log_time_us = now_us;
                        const char *app = (fs_view->toplevel && fs_view->toplevel->app_id) ?
                            fs_view->toplevel->app_id :
                            "app";
                        wlr_log(
                            WLR_INFO,
                            "[PACING-DIRECT-BLIT] Output '%s': App '%s' Direct 1-Pass Blit "
                            "Active | Flips=%lu (Direct 1-Pass=%lu, Composited=%lu)",
                            wlr_output->name, app, (unsigned long)output->flip_count,
                            (unsigned long)output->direct_draw_flips,
                            (unsigned long)output->composited_flips);
                    }

                    if (output == instance->vsync_output)
                    {
                        intptr_t baton = atomic_exchange(&instance->vsync_baton, 0);
                        if (baton != 0)
                        {
                            uint64_t current_time    = instance->embedder_api.GetCurrentTime();
                            uint64_t frame_target_ns =
                                (uint64_t)(refresh_budget_ms * 1000000.0);
                            instance->embedder_api.OnVsync(instance->engine, baton,
                                current_time,
                                current_time + frame_target_ns);
                        }
                    }

                    struct timespec ts_done;
                    clock_gettime(CLOCK_MONOTONIC, &ts_done);
                    output_send_frame_done_all(output, &ts_done);

                    pixman_region32_fini(&render_damage);
                    pixman_region32_fini(&frame_damage);
                    pixman_region32_fini(&vis_damage);
                    return;
                }
            }

            wlr_output_state_finish(&direct_blit_state);
        }
    }

    // TIER 3: Flutter-centric Compositing
    output->composited_flips++;
    if (output->direct_mode_active)
    {
        wlr_log(WLR_INFO,
            "[DIRECT-SCANOUT] Output '%s': Direct Scanout DISENGAGED (DS: OFF, "
            "returning to Flutter compositing)",
            wlr_output->name);
        output->direct_mode_active = false;
        output->scanout_test_cache.valid = false;

        // When leaving Direct Scanout, the GLES backbuffers in the swapchain are
        // stale. We must re-damage the full screen across all buffers in history!
        pixman_region32_union_rect(&render_damage, &render_damage, 0, 0,
            wlr_output->width, wlr_output->height);
#ifdef DAMAGE_HISTORY
        sparrow_output_damage_history_reset(output, wlr_output->width,
            wlr_output->height);
#endif
        if (instance->engine != nullptr)
        {
            SparrowView *v = nullptr;
            wl_list_for_each(v, &instance->views_list, link)
            {
                if (v->xdg_surface && v->xdg_surface->surface &&
                    v->xdg_surface->surface->mapped && sparrow_view_is_visible(v))
                {
                    if (v->texture_registered)
                    {
                        instance->embedder_api.MarkExternalTextureFrameAvailable(
                            instance->engine, v->texture_id);
                    }

                    SparrowSubSurface *sub = nullptr;
                    wl_list_for_each(sub, &v->subsurfaces, link)
                    {
                        if (sub->surface && sub->surface->mapped &&
                            sub->texture_registered)
                        {
                            instance->embedder_api.MarkExternalTextureFrameAvailable(
                                instance->engine, sub->texture_id);
                        }
                    }
                }
            }

            SparrowLayerSurface *ls = nullptr;
            wl_list_for_each(ls, &instance->layer_surfaces_list, link)
            {
                if (ls->layer_surface && ls->layer_surface->surface &&
                    ls->layer_surface->surface->mapped && ls->texture_registered)
                {
                    instance->embedder_api.MarkExternalTextureFrameAvailable(
                        instance->engine, ls->texture_id);
                }
            }
        }
    }

    // Begin render pass - Flutter-centric compositing
    struct wlr_output_state output_state;
    wlr_output_state_init(&output_state);

    // Set damage on output state for partial updates / scanout
    wlr_output_state_set_damage(&output_state, &render_damage);

    struct wlr_render_pass *render_pass =
        wlr_output_begin_render_pass(wlr_output, &output_state, nullptr);

    if (render_pass == nullptr)
    {
        pixman_region32_fini(&render_damage);
        pixman_region32_fini(&frame_damage);
        pixman_region32_fini(&vis_damage);
        wlr_output_state_finish(&output_state);
        wlr_output_schedule_frame(wlr_output);
        return;
    }

    // Clear to dark background only if Flutter hasn't produced scene layers yet
    bool has_scene_layers = false;
    {
        std::lock_guard<std::recursive_mutex> lock(
            instance->sparrow_renderer.render_mutex);
        has_scene_layers =
            (instance->sparrow_renderer.current_scene.layers_count > 0);
    }

    if (!has_scene_layers)
    {
        struct wlr_render_rect_options opts = {
            .box   = {.x = 0,
                .y = 0,
                .width  = wlr_output->width,
                .height = wlr_output->height},
            .color = {0.0f, 0.0f, 0.0f, 1.0f},
#ifdef DAMAGE_HISTORY
            .clip = &render_damage,
#endif
        };
        wlr_render_pass_add_rect(render_pass, &opts);
    }

    // Update scene node positions for input hit-testing
    // (Scene nodes are still used for hit-testing even though we render directly)
    sparrow_renderer_update_scene_positions();

    // Get this output's position in the layout for viewport offset
    struct wlr_box output_box;
    wlr_output_layout_get_box(instance->output_layout, wlr_output, &output_box);
    int eff_width = 0, eff_height = 0;
    wlr_output_effective_resolution(wlr_output, &eff_width, &eff_height);
    struct sparrow_output_viewport viewport = {
        .x     = output_box.x,
        .y     = output_box.y,
        .width = eff_width,
        .height = eff_height,
        .buffer_width  = wlr_output->width,
        .buffer_height = wlr_output->height,
        .transform     = wlr_output->transform,
    };

    double refresh_budget_ms = 0.0;

    bool submit_ok = false;
    {
        std::lock_guard<std::recursive_mutex> lock(
            instance->sparrow_renderer.render_mutex);

        {
            SPARROW_GL_SCOPE("Sparrow::ScenePass");
            // Render the Flutter scene directly (GPU textures + platform views)
            // This is the Flutter-centric path: NO CPU READBACK
            // Each output renders its portion of the unified coordinate space
            sparrow_renderer_render_scene(render_pass, &viewport,
#ifdef DAMAGE_HISTORY
                &render_damage
#else
                nullptr
#endif
            );
        }

        // Live damage visualization: highlight client damaged rectangles with thick
        // 4px borders
        if (instance->debug_damage && pixman_region32_not_empty(&vis_damage))
        {
            render_damage_rainbow(render_pass, &vis_damage);
        }

        // Render software cursors on top of everything (no-op if hardware cursor
        // plane is active)
        wlr_output_add_software_cursors_to_render_pass(wlr_output, render_pass,
            nullptr);

        // Render FPS & latency debug OSD if enabled
        struct timespec t_render_now;
        clock_gettime(CLOCK_MONOTONIC, &t_render_now);
        double cur_render_ms =
            ((t_render_now.tv_sec - t_frame_start.tv_sec) * 1000.0) +
            ((t_render_now.tv_nsec - t_frame_start.tv_nsec) / 1000000.0);
        output->smoothed_render_ms =
            (output->smoothed_render_ms <= 0.0) ?
            cur_render_ms :
            (output->smoothed_render_ms * 0.9 + cur_render_ms * 0.1);

        refresh_budget_ms =
            (wlr_output->current_mode && wlr_output->current_mode->refresh > 0) ?
            (1000000.0 / wlr_output->current_mode->refresh) :
            16.666;

        // Buffering mode evaluation (Controlled by F9 / instance->buffering_mode):
        if (instance->buffering_mode == Core::BUFFERING_DOUBLE)
        {
            output->triple_buffering_active = false;
        } else if (instance->buffering_mode == Core::BUFFERING_TRIPLE)
        {
            output->triple_buffering_active = true;
        } else // BUFFERING_AUTO
        {
            if (output->smoothed_render_ms > (0.80 * refresh_budget_ms))
            {
                output->triple_buffering_active = true;
            } else if (output->smoothed_render_ms < (0.50 * refresh_budget_ms))
            {
                output->triple_buffering_active = false;
            }
        }

        // Render FPS & latency debug OSD if enabled
        if (instance->show_fps)
        {
            SPARROW_GL_SCOPE("Sparrow::FPS_OSD");
            render_fps_osd(render_pass, output);
        }

        pixman_region32_fini(&render_damage);
        pixman_region32_fini(&frame_damage);
        pixman_region32_fini(&vis_damage);

        // Submit render pass
        submit_ok = wlr_render_pass_submit(render_pass);
    }

    if (!submit_ok)
    {
        wlr_output_state_finish(&output_state);
        wlr_output_schedule_frame(wlr_output);
        return;
    }

    // Mark visible surfaces on THIS output for presentation feedback BEFORE
    // output commit
    SparrowView *view = nullptr;
    wl_list_for_each(view, &instance->views_list, link)
    {
        if ((view->xdg_surface != nullptr) &&
            (view->xdg_surface->surface != nullptr) &&
            view->xdg_surface->surface->mapped &&
            ((view->current_output == output) ||
             (view->current_output == nullptr)) &&
            sparrow_view_is_visible(view))
        {
            wlr_surface_for_each_surface(view->xdg_surface->surface,
                send_presentation_iterator, wlr_output);
        }
    }
    if (instance->popups)
    {
        for (const auto &[handle, value] : *instance->popups)
        {
            auto *popup = static_cast<SparrowPopup*>(value);
            if ((popup != nullptr) && (popup->xdg_surface != nullptr) &&
                (popup->xdg_surface->surface != nullptr) &&
                popup->xdg_surface->surface->mapped &&
                (!popup->parent_view ||
                 sparrow_view_is_visible(popup->parent_view)))
            {
                wlr_surface_for_each_surface(popup->xdg_surface->surface,
                    send_presentation_iterator, wlr_output);
            }
        }
    }

    SparrowLayerSurface *ls_pres = nullptr;
    wl_list_for_each(ls_pres, &instance->layer_surfaces_list, link)
    {
        if ((ls_pres->layer_surface != nullptr) &&
            (ls_pres->layer_surface->surface != nullptr) &&
            ls_pres->layer_surface->surface->mapped &&
            ((ls_pres->current_output == output) ||
             (ls_pres->current_output == nullptr)))
        {
            wlr_surface_for_each_surface(ls_pres->layer_surface->surface,
                send_presentation_iterator, wlr_output);
        }
    }

    // Commit the output
    {
        SPARROW_TRACE_SCOPE("Output::CommitState");
        if (wlr_output_commit_state(wlr_output, &output_state))
        {
            sparrow_output_mark_hw_cursor_committed(output);
        }

        wlr_output_state_finish(&output_state);

        if (instance != nullptr)
        {
            engine_platform_task_queue_process(instance);
            std::lock_guard<std::recursive_mutex> lock(
                instance->sparrow_renderer.render_mutex);
            if (!instance->lingering_sensitive_boxes.empty())
            {
                wlr_output_schedule_frame(wlr_output);
            }
        }
    }

#if defined (SPARROW_ENABLE_TRACE)
    SparrowTrace::instance().end_renderdoc_frame_if_capturing(s_frame_index);
#endif

    output->flip_count++;
    double flip_dt_ms =
        (output->last_flip_time_us > 0) ?
        (double)(now_us - output->last_flip_time_us) / 1000.0 :
        0.0;
    output->last_flip_time_us = now_us;

    if ((flip_dt_ms > (1.35 * refresh_budget_ms)) && (output->flip_count > 10))
    {
        output->missed_vblank_count++;
        if (instance->debug_pacing)
        {
            wlr_log(WLR_INFO,
                "[PACING-VBLANK-MISS] Output '%s' frame interval spike: %.2fms "
                "(target: %.2fms, missed: %lu)",
                wlr_output->name, flip_dt_ms, refresh_budget_ms,
                (unsigned long)output->missed_vblank_count);
        }
    }

    // Periodic health summary log every 2 seconds if debug_pacing is active
    if (instance->debug_pacing &&
        (now_us - output->last_pacing_log_time_us >= 2000000ULL))
    {
        double dt_s = (output->last_pacing_log_time_us > 0) ?
            (double)(now_us - output->last_pacing_log_time_us) / 1000000.0 :
            2.0;
        output->last_pacing_log_time_us = now_us;
        SparrowView *active_view = nullptr;
        wl_list_for_each(active_view, &instance->views_list, link)
        {
            const bool has_subsurfaces = !wl_list_empty(&active_view->subsurfaces);
            if ((active_view->commit_count > 0) || has_subsurfaces)
            {
                const char *app =
                    (active_view->toplevel && active_view->toplevel->app_id) ?
                    active_view->toplevel->app_id :
                    "app";

                if (active_view->commit_count > 0)
                {
                    double commit_fps = (dt_s > 0) ?
                        (double)(active_view->commit_count - active_view->last_summary_commits) / dt_s :
                        0.0;
                    double sample_fps = (dt_s > 0) ?
                        (double)(active_view->sampled_count - active_view->last_summary_samples) / dt_s :
                        0.0;
                    double ghost_fps = (dt_s > 0) ?
                        (double)(active_view->duplicate_sample_count - active_view->last_summary_ghosts) /
                        dt_s :
                        0.0;
                    active_view->last_summary_commits = active_view->commit_count;
                    active_view->last_summary_samples = active_view->sampled_count;
                    active_view->last_summary_ghosts  = active_view->duplicate_sample_count;

                    wlr_log(WLR_INFO,
                        "[PACING-SUMMARY] App '%s': Commits=%lu (%.1f fps) | Sampled=%lu (%.1f fps) | "
                        "DroppedOverwritten=%lu | GhostDupes=%lu (%.1f fps) || KMS: Flips=%lu | "
                        "VBlankMisses=%lu",
                        app, (unsigned long)active_view->commit_count, commit_fps,
                        (unsigned long)active_view->sampled_count, sample_fps,
                        (unsigned long)active_view->dropped_buffer_count,
                        (unsigned long)active_view->duplicate_sample_count, ghost_fps,
                        (unsigned long)output->flip_count,
                        (unsigned long)output->missed_vblank_count);
                }

                SparrowSubSurface *sub = nullptr;
                wl_list_for_each(sub, &active_view->subsurfaces, link)
                {
                    if ((sub->commit_count > 0) || (sub->sampled_count > 0))
                    {
                        double sub_commit_fps = (dt_s > 0) ?
                            (double)(sub->commit_count - sub->last_summary_commits) / dt_s :
                            0.0;
                        double sub_sample_fps = (dt_s > 0) ?
                            (double)(sub->sampled_count - sub->last_summary_samples) / dt_s :
                            0.0;
                        double sub_ghost_fps = (dt_s > 0) ?
                            (double)(sub->duplicate_sample_count - sub->last_summary_ghosts) / dt_s :
                            0.0;
                        sub->last_summary_commits = sub->commit_count;
                        sub->last_summary_samples = sub->sampled_count;
                        sub->last_summary_ghosts  = sub->duplicate_sample_count;

                        wlr_log(WLR_INFO,
                            "[PACING-SUMMARY-SUB] App '%s' (SubSurface %u): Commits=%lu (%.1f fps) | "
                            "Sampled=%lu (%.1f fps) | DroppedOverwritten=%lu | GhostDupes=%lu (%.1f fps)",
                            app, sub->handle,
                            (unsigned long)sub->commit_count, sub_commit_fps,
                            (unsigned long)sub->sampled_count, sub_sample_fps,
                            (unsigned long)sub->dropped_buffer_count,
                            (unsigned long)sub->duplicate_sample_count, sub_ghost_fps);
                    }
                }
            }
        }
    }

    // Dispatch VSync baton for the designated vsync output immediately after KMS
    // commit so Flutter has the full upcoming refresh cycle budget to render the
    // next frame on time.
    output->vsync_dispatched_in_frame = false;
    if (output == instance->vsync_output)
    {
        intptr_t baton = atomic_exchange(&instance->vsync_baton, 0);
        if (baton != 0)
        {
            output->vsync_dispatched_in_frame = true;
            uint64_t current_time    = instance->embedder_api.GetCurrentTime();
            uint64_t frame_target_ns = (uint64_t)(refresh_budget_ms * 1000000.0);
            instance->embedder_api.OnVsync(instance->engine, baton, current_time,
                current_time + frame_target_ns);
        }
    }

    // Send frame done to Wayland surfaces that requested a frame callback on this
    // output
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    output_send_frame_done_all(output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data)
{
    Output *output = wl_container_of(listener, output, request_state);
    struct wlr_output_event_request_state *event =
        static_cast<wlr_output_event_request_state*>(data);

    if (wlr_output_commit_state(output->wlr_output, event->state))
    {
        sparrow_output_mark_hw_cursor_committed(output);
    }

    output->scanout_test_cache.valid = false;
    output->last_state_change_us     = get_monotonic_now_us();

#ifdef DAMAGE_HISTORY
    sparrow_output_damage_history_reset(output, output->wlr_output->width,
        output->wlr_output->height);
#endif
    {
        std::lock_guard<std::mutex> lock(output->damage_mutex);
        wlr_damage_ring_add_whole(&output->damage_ring);
    }

    // Update Flutter window metrics with TOTAL layout bounds, not just this
    // output Using single-output dimensions here was causing severe rendering
    // bugs in multi-monitor setups
    const auto instance = Core::instance();
    if (instance->engine != nullptr)
    {
        struct wlr_box total_box = {};
        wlr_output_layout_get_box(instance->output_layout, nullptr, &total_box);

        if ((total_box.width > 0) && (total_box.height > 0))
        {
            if (instance->cursor != nullptr)
            {
                double clamped_x = std::clamp(instance->cursor->x, 0.0,
                    static_cast<double>(total_box.width - 1));
                double clamped_y =
                    std::clamp(instance->cursor->y, 0.0,
                        static_cast<double>(total_box.height - 1));
                if ((clamped_x != instance->cursor->x) ||
                    (clamped_y != instance->cursor->y))
                {
                    wlr_cursor_warp(instance->cursor, nullptr, clamped_x, clamped_y);
                }
            }

            FlutterWindowMetricsEvent window_metrics = {};
            window_metrics.struct_size = sizeof(FlutterWindowMetricsEvent);
            window_metrics.width  = total_box.width;
            window_metrics.height = total_box.height;
            window_metrics.pixel_ratio = 1.0; // Use 1.0 for multi-output; per-output
                                              // scaling is handled separately
            wlr_log(WLR_INFO,
                "Output %s state changed, updated Flutter metrics: %dx%d",
                output->wlr_output->name, total_box.width, total_box.height);
            if (instance->embedder_api.SendWindowMetricsEvent != nullptr)
            {
                instance->embedder_api.SendWindowMetricsEvent(instance->engine,
                    &window_metrics);
            }

            send_output_changed(output);
            refresh_views_after_output_change(instance);

            sparrow_popups_update_on_output_change(output);
            sparrow_layer_surfaces_update_on_output_change(output);

            if (sparrow_is_session_locked())
            {
                sparrow_session_lock_configure_output(output);
            }
        }
    }
}

static void output_present(struct wl_listener *listener, void *data)
{
    Output *output = wl_container_of(listener, output, present);
    const auto instance = Core::instance();
    struct wlr_output_event_present *event =
        static_cast<wlr_output_event_present*>(data);

    if (event->presented)
    {
        output->last_present_time_ns =
            static_cast<uint64_t>(event->when.tv_sec) * 1000000000ULL +
            event->when.tv_nsec;
        if (event->refresh > 0)
        {
            output->refresh_duration_ns = static_cast<uint64_t>(event->refresh);
        } else
        {
            int refresh_mhz = get_output_refresh(output->wlr_output);
            if (refresh_mhz > 0)
            {
                output->refresh_duration_ns = 1000000000000ULL / refresh_mhz;
            }
        }

        output->current_cycle_seq++;
    }

    // Only handle vsync baton for the designated vsync output
    if (output != instance->vsync_output)
    {
        return;
    }

    output->vsync_dispatched_in_frame = false;

    intptr_t baton = atomic_exchange(&instance->vsync_baton, 0);
    if (baton != 0)
    {
        uint64_t current_time = instance->embedder_api.GetCurrentTime();

        uint64_t frame_target_ns = event->refresh;
        if (frame_target_ns == 0)
        {
            frame_target_ns = 16600000;
        }

        // Apply rate limit if set (for power saving)
        if (instance->vsync_rate_limit > 0)
        {
            uint64_t limited_ns = 1000000000ULL / instance->vsync_rate_limit;
            if (limited_ns > frame_target_ns)
            {
                frame_target_ns = limited_ns;
            }
        }

        instance->embedder_api.OnVsync(instance->engine, baton, current_time,
            current_time + frame_target_ns);
    }
}

void sparrow_engine_vsync_callback(void *data, intptr_t baton)
{
    const auto instance = Core::instance();

    atomic_store(&instance->vsync_baton, baton);

    instance->callable_queue.enqueue([instance] ()
    {
        Output *vsync_out = instance->vsync_output;
        if ((vsync_out != nullptr) && (vsync_out->wlr_output != nullptr) &&
            vsync_out->wlr_output->enabled)
        {
            if (!vsync_out->wlr_output->frame_pending)
            {
                wlr_output_schedule_frame(vsync_out->wlr_output);
            }
        } else
        {
            Output *out = nullptr;
            wl_list_for_each(out, &instance->outputs, link)
            {
                if (out->wlr_output && out->wlr_output->enabled &&
                    !out->wlr_output->frame_pending)
                {
                    wlr_output_schedule_frame(out->wlr_output);
                }
            }
        }
    });
}

// Handle output disconnect/hotplug removal
static void output_destroy(struct wl_listener *listener, void *data)
{
    Output *output = wl_container_of(listener, output, destroy);
    const auto instance = Core::instance();
    if (instance == nullptr)
    {
        return;
    }

    const uint32_t output_id    = output->id;
    const bool was_vsync_output = (output == instance->vsync_output);

    wlr_log(WLR_INFO, "Output %s (id=%d) disconnected",
        output->wlr_output ? output->wlr_output->name : "unknow", output_id);

    // Remove from output layout FIRST so it doesn't query a destroyed output
    if ((instance->output_layout != nullptr) && (output->wlr_output != nullptr))
    {
        wlr_output_layout_remove(instance->output_layout, output->wlr_output);
    }

    // Remove listeners safely
    if ((output->frame.link.prev != nullptr) &&
        (output->frame.link.next != nullptr))
    {
        wl_list_remove(&output->frame.link);
        wl_list_init(&output->frame.link);
    }

    if ((output->request_state.link.prev != nullptr) &&
        (output->request_state.link.next != nullptr))
    {
        wl_list_remove(&output->request_state.link);
        wl_list_init(&output->request_state.link);
    }

    if ((output->present.link.prev != nullptr) &&
        (output->present.link.next != nullptr))
    {
        wl_list_remove(&output->present.link);
        wl_list_init(&output->present.link);
    }

    if ((output->needs_frame.link.prev != nullptr) &&
        (output->needs_frame.link.next != nullptr))
    {
        wl_list_remove(&output->needs_frame.link);
        wl_list_init(&output->needs_frame.link);
    }

    if ((output->damage.link.prev != nullptr) &&
        (output->damage.link.next != nullptr))
    {
        wl_list_remove(&output->damage.link);
        wl_list_init(&output->damage.link);
    }

    if ((output->destroy.link.prev != nullptr) &&
        (output->destroy.link.next != nullptr))
    {
        wl_list_remove(&output->destroy.link);
        wl_list_init(&output->destroy.link);
    }

    // Remove from linked list safely
    if ((output->link.prev != nullptr) && (output->link.next != nullptr))
    {
        wl_list_remove(&output->link);
        wl_list_init(&output->link);
    }

    // Destroy scene output (this also removes it from scene_output_layout)
    if ((output->scene_output != nullptr) && (instance->scene != nullptr))
    {
        wlr_scene_output_destroy(output->scene_output);
        output->scene_output = nullptr;
    } else
    {
        output->scene_output = nullptr;
    }

    // Clear vsync_output if this was it
    if (was_vsync_output)
    {
        instance->vsync_output = nullptr;
    }

    if (output->osd_texture != nullptr)
    {
        wlr_texture_destroy(output->osd_texture);
        output->osd_texture = nullptr;
    }

    if (output->vblank_timer_source != nullptr)
    {
        wl_event_source_remove(output->vblank_timer_source);
        output->vblank_timer_source = nullptr;
    }

    if (output->vblank_timer_fd >= 0)
    {
        close(output->vblank_timer_fd);
        output->vblank_timer_fd = -1;
    }

    wlr_damage_ring_finish(&output->damage_ring);
    pixman_region32_fini(&output->client_damage);
#ifdef DAMAGE_HISTORY
    for (int i = 0; i < NUM_DAMAGE_HISTORY; i++)
    {
        pixman_region32_fini(&output->damage_history[i]);
    }

#endif
    delete (output);

    // If this was the vsync output, select a new one
    if (was_vsync_output)
    {
        sparrow_select_highest_refresh_output();
    }

    // Update Flutter window metrics only if engine and layout are still active
    if ((instance->engine != nullptr) && (instance->output_layout != nullptr))
    {
        struct wlr_box total_box = {};
        wlr_output_layout_get_box(instance->output_layout, nullptr, &total_box);

        if ((instance->embedder_api.SendWindowMetricsEvent != nullptr) &&
            (total_box.width > 0) && (total_box.height > 0))
        {
            FlutterWindowMetricsEvent window_metrics = {};
            window_metrics.struct_size = sizeof(FlutterWindowMetricsEvent);
            window_metrics.width  = total_box.width;
            window_metrics.height = total_box.height;
            window_metrics.pixel_ratio =
                1.0; // Use default, apps handle per-output scaling
            instance->embedder_api.SendWindowMetricsEvent(instance->engine,
                &window_metrics);
        }

        send_output_removed(output_id);
        sparrow_output_manager_update();
        sparrow_output_update_dpms_lifecycle();
    }
}

void sparrow_server_new_output(struct wl_listener *listener, void *data)
{
    (void)listener;
    const auto instance = Core::instance();
    struct wlr_output *wlr_output = static_cast<struct wlr_output*>(data);

    wlr_log(WLR_INFO, "New output detected: %s (%s %s)", wlr_output->name,
        wlr_output->make ? wlr_output->make : "unknown",
        wlr_output->model ? wlr_output->model : "unknown");

    wlr_output_init_render(wlr_output, instance->allocator, instance->renderer);

    Output *output = new Output();
    if (!output)
    {
        wlr_log(WLR_ERROR, "Failed to allocate sparrow_output");
        return;
    }

    output->id = ++instance->next_output_id; // Assign unique ID
    output->wlr_output = wlr_output;
    wlr_damage_ring_init(&output->damage_ring);
    pixman_region32_init(&output->client_damage);
#ifdef DAMAGE_HISTORY
    for (int i = 0; i < NUM_DAMAGE_HISTORY; i++)
    {
        pixman_region32_init(&output->damage_history[i]);
    }

    sparrow_output_damage_history_reset(output, wlr_output->width,
        wlr_output->height);
    wlr_log(WLR_INFO,
        "[TRIPLE_BUFFERING] Output %s: Triple Buffering active (swapchain "
        "capacity=%d, damage history ring=%d)",
        wlr_output->name, WLR_SWAPCHAIN_CAP, NUM_DAMAGE_HISTORY);
#endif
    // output->scene_output = wlr_scene_output_create(instance->scene,
    // wlr_output);

    // Set up listeners
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    output->present.notify = output_present;
    wl_signal_add(&wlr_output->events.present, &output->present);
    output->needs_frame.notify = output_handle_needs_frame;
    wl_signal_add(&wlr_output->events.needs_frame, &output->needs_frame);
    output->damage.notify = output_handle_damage;
    wl_signal_add(&wlr_output->events.damage, &output->damage);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    output->vblank_timer_fd =
        timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (output->vblank_timer_fd >= 0)
    {
        output->vblank_timer_source = wl_event_loop_add_fd(
            instance->wl_event_loop, output->vblank_timer_fd, WL_EVENT_READABLE,
            output_vblank_timer_handler, output);
    }

    // Add to linked list
    wl_list_insert(&instance->outputs, &output->link);

    // Configure output mode
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    // wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != nullptr)
    {
        wlr_output_state_set_mode(&state, mode);
        wlr_log(WLR_INFO, "Output %s: setting mode %dx%d @ %d mHz",
            wlr_output->name, mode->width, mode->height, mode->refresh);
    }

    // Explicitly set scale to 1.0 to ensure clients don't get confused
    // on multi-monitor setups. User can change via output settings later.
    wlr_output_state_set_scale(&state, 1.0);
    wlr_log(WLR_INFO, "Output %s: explicitly setting scale to 1.0",
        wlr_output->name);

    wlr_output_state_set_enabled(&state, true);

    // Enable Adaptive Sync (VRR) if supported by the display hardware
    if (wlr_output->adaptive_sync_supported)
    {
        wlr_output_state_set_adaptive_sync_enabled(&state, true);
        if (!wlr_output_test_state(wlr_output, &state))
        {
            wlr_output_state_set_adaptive_sync_enabled(&state, false);
        } else
        {
            wlr_log(WLR_INFO, "Output %s: Adaptive Sync (VRR) enabled",
                wlr_output->name);
        }
    }

    if (!wlr_output_commit_state(wlr_output, &state))
    {
        wlr_log(WLR_ERROR, "Failed to commit output state for %s",
            wlr_output->name);
        wlr_output_state_finish(&state);
        // Clean up on failure
        if (output->vblank_timer_source != nullptr)
        {
            wl_event_source_remove(output->vblank_timer_source);
            output->vblank_timer_source = nullptr;
        }

        if (output->vblank_timer_fd >= 0)
        {
            close(output->vblank_timer_fd);
            output->vblank_timer_fd = -1;
        }

        wl_list_remove(&output->frame.link);
        wl_list_remove(&output->request_state.link);
        wl_list_remove(&output->present.link);
        wl_list_remove(&output->needs_frame.link);
        wl_list_remove(&output->damage.link);
        wl_list_remove(&output->destroy.link);
        wl_list_remove(&output->link);
        return;
    }

    wlr_output_state_finish(&state);
    sparrow_output_mark_hw_cursor_committed(output);

    // Add to output layout
    output->layout_output =
        wlr_output_layout_add_auto(instance->output_layout, wlr_output);
    // if (output->layout_output != nullptr && output->scene_output != nullptr &&
    // instance->scene_output_layout != nullptr) {
    // wlr_scene_output_layout_add_output(instance->scene_output_layout,
    // output->layout_output,
    // output->scene_output);
    // }

    // Select vsync output (highest refresh rate)
    sparrow_select_highest_refresh_output();

    wlr_output_schedule_frame(wlr_output);

    // Update Flutter window metrics with total bounds
    struct wlr_box total_box = {};
    wlr_output_layout_get_box(instance->output_layout, nullptr, &total_box);

    if ((instance->engine != nullptr) &&
        (instance->embedder_api.SendWindowMetricsEvent != nullptr))
    {
        FlutterWindowMetricsEvent window_metrics = {};
        window_metrics.struct_size = sizeof(FlutterWindowMetricsEvent);
        window_metrics.width  = total_box.width;
        window_metrics.height = total_box.height;
        window_metrics.pixel_ratio =
            1.0; // Use default, apps handle per-output scaling

        instance->embedder_api.SendWindowMetricsEvent(instance->engine,
            &window_metrics);
        wlr_log(WLR_INFO, "Updated Flutter window metrics: %dx%d", total_box.width,
            total_box.height);
    }

    // instance->callable_queue.enqueue([=] {
    // Send platform channel message for output_added
    send_output_added(output);
    sparrow_output_manager_update();
    sparrow_output_update_dpms_lifecycle();
}

// Helper to get first output from list (for backwards compatibility)
// Returns nullptr if no outputs are connected
Output *sparrow_get_first_output()
{
    const auto instance = Core::instance();

    if (wl_list_empty(&instance->outputs))
    {
        return nullptr;
    }

    Output *first;
    return wl_container_of(instance->outputs.next, first, link);
}

// Output manager implementation (wlr_output_manager_v1)
void sparrow_output_manager_update()
{
    const auto instance = Core::instance();

    if ((instance == nullptr) || (instance->output_manager == nullptr) ||
        wl_list_empty(&instance->outputs))
    {
        return;
    }

    struct wlr_output_configuration_v1 *config =
        wlr_output_configuration_v1_create();
    if (config == nullptr)
    {
        return;
    }

    Output *output;
    wl_list_for_each(output, &instance->outputs, link)
    {
        struct wlr_output *wlr_out = output->wlr_output;
        if (wlr_out == nullptr)
        {
            continue;
        }

        struct wlr_output_configuration_head_v1 *config_head =
            wlr_output_configuration_head_v1_create(config, wlr_out);
        if (config_head == nullptr)
        {
            continue;
        }

        struct wlr_box box = {};
        wlr_output_layout_get_box(instance->output_layout, wlr_out, &box);

        config_head->state.enabled = wlr_out->enabled;
        config_head->state.mode    = wlr_out->current_mode;
        if ((wlr_out->current_mode == nullptr) && wlr_out->enabled)
        {
            config_head->state.custom_mode.width   = wlr_out->width;
            config_head->state.custom_mode.height  = wlr_out->height;
            config_head->state.custom_mode.refresh = get_output_refresh(wlr_out);
        }

        config_head->state.x     = box.x;
        config_head->state.y     = box.y;
        config_head->state.scale = wlr_out->scale;
        config_head->state.transform = wlr_out->transform;
    }

    wlr_output_manager_v1_set_configuration(instance->output_manager, config);
}

static void sanitize_output_state_for_backend(struct wlr_output *wlr_out,
    struct wlr_output_state *state)
{
    // If adaptive sync status hasn't changed or isn't supported, clear the flag
    if (state->committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED)
    {
        bool current_adaptive =
            (wlr_out->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED);
        if ((state->adaptive_sync_enabled == current_adaptive) ||
            !wlr_out->adaptive_sync_supported)
        {
            state->committed &= ~WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;
        }
    }

    // Wayland nested backend doesn't support custom refresh rates or disabling
    // adaptive sync
    if (wlr_output_is_wl(wlr_out))
    {
        if (state->committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED)
        {
            state->committed &= ~WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;
        }

        if (state->committed & WLR_OUTPUT_STATE_MODE)
        {
            if ((wlr_out->width == state->custom_mode.width) &&
                (wlr_out->height == state->custom_mode.height))
            {
                // Mode dimensions are unchanged, avoid triggering mode switch
                state->committed &= ~WLR_OUTPUT_STATE_MODE;
            } else
            {
                state->custom_mode.refresh = 0;
            }
        }
    }
}

static void handle_output_manager_apply(struct wl_listener *listener,
    void *data)
{
    const auto instance = Core::instance();
    struct wlr_output_configuration_v1 *config =
        static_cast<struct wlr_output_configuration_v1*>(data);

    bool ok = true;
    struct wlr_output_configuration_head_v1 *config_head;
    wl_list_for_each(config_head, &config->heads, link)
    {
        struct wlr_output *wlr_out = config_head->state.output;
        if (wlr_out == nullptr)
        {
            continue;
        }

        struct wlr_output_state state;
        wlr_output_state_init(&state);

        if (config_head->state.enabled)
        {
            wlr_output_head_v1_state_apply(&config_head->state, &state);
            sanitize_output_state_for_backend(wlr_out, &state);

            if (!wlr_output_commit_state(wlr_out, &state))
            {
                wlr_log(WLR_ERROR, "Failed to commit output state for %s",
                    wlr_out->name);
                ok = false;
            } else
            {
                wlr_output_layout_add(instance->output_layout, wlr_out,
                    config_head->state.x, config_head->state.y);
            }
        } else
        {
            wlr_output_state_set_enabled(&state, false);
            if (!wlr_output_commit_state(wlr_out, &state))
            {
                wlr_log(WLR_ERROR, "Failed to disable output %s", wlr_out->name);
                ok = false;
            } else
            {
                wlr_output_layout_remove(instance->output_layout, wlr_out);
            }
        }

        wlr_output_state_finish(&state);
    }

    if (ok)
    {
        wlr_output_configuration_v1_send_succeeded(config);
    } else
    {
        wlr_output_configuration_v1_send_failed(config);
    }

    wlr_output_configuration_v1_destroy(config);

    // Update advertised output manager configuration
    sparrow_output_manager_update();

    // Notify Flutter and update layout / window metrics
    if (!wl_list_empty(&instance->outputs))
    {
        struct wlr_box total_box = {};
        wlr_output_layout_get_box(instance->output_layout, nullptr, &total_box);

        if ((total_box.width > 0) && (total_box.height > 0))
        {
            if (instance->cursor != nullptr)
            {
                double clamped_x = std::clamp(instance->cursor->x, 0.0,
                    static_cast<double>(total_box.width - 1));
                double clamped_y =
                    std::clamp(instance->cursor->y, 0.0,
                        static_cast<double>(total_box.height - 1));
                if ((clamped_x != instance->cursor->x) ||
                    (clamped_y != instance->cursor->y))
                {
                    wlr_cursor_warp(instance->cursor, nullptr, clamped_x, clamped_y);
                }
            }

            if (instance->engine != nullptr)
            {
                FlutterWindowMetricsEvent window_metrics = {};
                window_metrics.struct_size = sizeof(FlutterWindowMetricsEvent);
                window_metrics.width  = total_box.width;
                window_metrics.height = total_box.height;
                window_metrics.pixel_ratio = 1.0;
                if (instance->embedder_api.SendWindowMetricsEvent != nullptr)
                {
                    instance->embedder_api.SendWindowMetricsEvent(instance->engine,
                        &window_metrics);
                }

                Output *output;
                const uint64_t now_us = get_monotonic_now_us();
                wl_list_for_each(output, &instance->outputs, link)
                {
                    output->scanout_test_cache.valid = false;
                    output->last_state_change_us     = now_us;
                    send_output_changed(output);
                    if (output->wlr_output && output->wlr_output->enabled)
                    {
#ifdef DAMAGE_HISTORY
                        sparrow_output_damage_history_reset(
                            output, output->wlr_output->width, output->wlr_output->height);
#endif
                        {
                            std::lock_guard<std::mutex> lock(output->damage_mutex);
                            wlr_damage_ring_add_whole(&output->damage_ring);
                        }
                    }
                }
                sparrow_damage_add_box(nullptr);
                refresh_views_after_output_change(instance);

                wl_list_for_each(output, &instance->outputs, link)
                {
                    sparrow_popups_update_on_output_change(output);
                    sparrow_layer_surfaces_update_on_output_change(output);
                }

                if (sparrow_is_session_locked())
                {
                    sparrow_session_lock_configure_all();
                }
            }
        }
    }

    sparrow_output_update_dpms_lifecycle();
}

static void handle_output_manager_test(struct wl_listener *listener,
    void *data)
{
    struct wlr_output_configuration_v1 *config =
        static_cast<struct wlr_output_configuration_v1*>(data);

    bool ok = true;
    struct wlr_output_configuration_head_v1 *config_head;
    wl_list_for_each(config_head, &config->heads, link)
    {
        struct wlr_output *wlr_out = config_head->state.output;
        if (wlr_out == nullptr)
        {
            continue;
        }

        struct wlr_output_state state;
        wlr_output_state_init(&state);

        if (config_head->state.enabled)
        {
            wlr_output_head_v1_state_apply(&config_head->state, &state);
            sanitize_output_state_for_backend(wlr_out, &state);
            if (!wlr_output_test_state(wlr_out, &state))
            {
                ok = false;
            }
        }

        wlr_output_state_finish(&state);

        if (!ok)
        {
            break;
        }
    }

    if (ok)
    {
        wlr_output_configuration_v1_send_succeeded(config);
    } else
    {
        wlr_output_configuration_v1_send_failed(config);
    }

    wlr_output_configuration_v1_destroy(config);
}

void sparrow_output_manager_init()
{
    const auto instance = Core::instance();

    instance->output_manager = wlr_output_manager_v1_create(instance->wl_display);
    if (instance->output_manager == nullptr)
    {
        wlr_log(WLR_ERROR, "Failed to create wlr_output_manager_v1");
        return;
    }

    instance->output_manager_apply.notify = handle_output_manager_apply;
    wl_signal_add(&instance->output_manager->events.apply,
        &instance->output_manager_apply);

    instance->output_manager_test.notify = handle_output_manager_test;
    wl_signal_add(&instance->output_manager->events.test,
        &instance->output_manager_test);

    sparrow_output_manager_update();
    wlr_log(WLR_INFO, "Initialized wlr_output_manager_v1");
}

static void handle_output_power_manager_set_mode(struct wl_listener *listener,
    void *data)
{
    (void)listener;
    struct wlr_output_power_v1_set_mode_event *event =
        static_cast<struct wlr_output_power_v1_set_mode_event*>(data);

    if ((event == nullptr) || (event->output == nullptr))
    {
        return;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);

    switch (event->mode)
    {
      case ZWLR_OUTPUT_POWER_V1_MODE_OFF:
        wlr_output_state_set_enabled(&state, false);
        wlr_log(WLR_INFO, "[DPMS] Output '%s' power mode: OFF",
            event->output->name);
        break;

      case ZWLR_OUTPUT_POWER_V1_MODE_ON:
        wlr_output_state_set_enabled(&state, true);
        wlr_log(WLR_INFO, "[DPMS] Output '%s' power mode: ON", event->output->name);
        break;
    }

    if (!wlr_output_commit_state(event->output, &state))
    {
        wlr_log(WLR_ERROR, "[DPMS] Failed to commit power state for output '%s'",
            event->output->name);
    } else
    {
        if (event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON)
        {
            sparrow_select_highest_refresh_output();
            sparrow_damage_add_box(nullptr);
            wlr_output_schedule_frame(event->output);
        }

        sparrow_output_update_dpms_lifecycle();
    }

    wlr_output_state_finish(&state);
}

void sparrow_output_power_manager_init()
{
    const auto instance = Core::instance();

    instance->output_power_manager =
        wlr_output_power_manager_v1_create(instance->wl_display);
    if (instance->output_power_manager == nullptr)
    {
        wlr_log(WLR_ERROR, "Failed to create wlr_output_power_manager_v1");
        return;
    }

    instance->output_power_manager_set_mode.notify =
        handle_output_power_manager_set_mode;
    wl_signal_add(&instance->output_power_manager->events.set_mode,
        &instance->output_power_manager_set_mode);

    wlr_log(WLR_INFO,
        "Initialized wlr_output_power_management_v1 (DPMS power control)");
}

bool sparrow_has_enabled_output()
{
    const auto instance = Core::instance();
    if ((instance == nullptr) || wl_list_empty(&instance->outputs))
    {
        return false;
    }

    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if ((output->wlr_output != nullptr) && output->wlr_output->enabled)
        {
            return true;
        }
    }

    return false;
}

void sparrow_output_update_dpms_lifecycle()
{
    const bool enabled = sparrow_has_enabled_output();
    sparrow_send_lifecycle_state_dpms(enabled);
}
