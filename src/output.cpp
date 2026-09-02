#include <cstddef>
#include <string>

#include <drm_fourcc.h>

#include <sys/eventfd.h>

#include <sparrow/nonstd/wlroots-full.hpp>

#include "core.hpp"
#include "flutter/platform/engine/messages/output_message.hpp"
#include "flutter/platform/task.hpp"
#include "output.hpp"
#include "renderer/renderer.hpp"
#include "surface/popup.hpp"
#include "surface/sub_surface.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"
#include "util/trace.hpp"

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
    Core *instance   = Core::instance();
    Output *best     = nullptr;
    int best_refresh = 0;

    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (!output || !output->wlr_output)
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
void sparrow_output_damage_history_reset(Output *output, int width, int height)
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

void sparrow_damage_add_box(const struct wlr_box *box, bool is_client_damage)
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return;
    }

    bool is_main_thread = (get_tid() == instance->platform_tid);
    Output *output;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (!output || !output->wlr_output || !output->wlr_output->enabled)
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

                pthread_mutex_lock(&output->damage_mutex);
                wlr_damage_ring_add_box(&output->damage_ring, &local_box);
                if (is_client_damage)
                {
                    pixman_region32_union_rect(&output->client_damage, &output->client_damage,
                        local_box.x, local_box.y,
                        local_box.width, local_box.height);
                }

                pthread_mutex_unlock(&output->damage_mutex);

                if (is_main_thread)
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

            pthread_mutex_lock(&output->damage_mutex);
            wlr_damage_ring_add_box(&output->damage_ring, &local_box);
            if (is_client_damage)
            {
                pixman_region32_union_rect(&output->client_damage, &output->client_damage,
                    local_box.x, local_box.y,
                    local_box.width, local_box.height);
            }

            pthread_mutex_unlock(&output->damage_mutex);

            if (is_main_thread)
            {
                wlr_output_schedule_frame(output->wlr_output);
            }
        }
    }

    if (!is_main_thread && (instance->platform_notify_fd >= 0))
    {
        eventfd_write(instance->platform_notify_fd, 1);
    }
}

// Set a specific output as the vsync driver (0 = auto/highest)
void sparrow_set_vsync_output(uint32_t output_id)
{
    Core *instance = Core::instance();
    if (output_id == 0)
    {
        sparrow_select_highest_refresh_output();
        return;
    }

    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (!output)
        {
            continue;
        }

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
    Core *instance = Core::instance();
    instance->vsync_rate_limit = max_hz;
    wlr_log(WLR_INFO, "Vsync rate limit set to: %d Hz (0 = unlimited)", max_hz);
}

// Find output by ID
static Output *find_output_by_id(uint32_t output_id)
{
    Core *instance = Core::instance();
    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (!output)
        {
            continue;
        }

        if (output->id == output_id)
        {
            return output;
        }
    }
    return nullptr;
}

// Multi-monitor support: determine which output a box is on (by center point)
Output *sparrow_output_for_box(int x, int y, int width,
    int height)
{
    Core *instance = Core::instance();

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
        if (!output)
        {
            continue;
        }

        if (output->wlr_output == wlr_out)
        {
            return output;
        }
    }

    // Shouldn't happen, but fallback
    return sparrow_get_first_output();
}

// Set output mode (resolution + refresh rate)
bool sparrow_set_output_mode(uint32_t output_id, int width,
    int height, int refresh)
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
#ifdef DAMAGE_HISTORY
    sparrow_output_damage_history_reset(sparrow_out, best_mode->width, best_mode->height);
#endif
    wlr_log(WLR_INFO, "Set output %s mode to %dx%d@%d", wlr_out->name,
        best_mode->width, best_mode->height, best_mode->refresh);
    sparrow_output_manager_update();
    return true;
}

// Set output position in layout
bool sparrow_set_output_position(uint32_t output_id, int x, int y)
{
    Core *instance = Core::instance();
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

// Render software cursor on the output
static void render_cursor(struct wlr_render_pass *render_pass,
    const sparrow_output_viewport *viewport)
{
    Core *instance = Core::instance();
    if ((instance->cursor == nullptr) || (instance->renderer == nullptr))
    {
        return;
    }

    if (!instance->cursor_visible)
    {
        return;
    }

    // Skip software cursor rendering in nested mode (host handles cursor)
    // if (is_nested_backend(instance->backend)) {
    // return;
    // }

    struct wlr_texture *cursor_texture = nullptr;
    int hotspot_x = 0;
    int hotspot_y = 0;

    // Try client-provided cursor surface first
    if ((instance->client_cursor_surface != nullptr) &&
        instance->client_cursor_surface->mapped)
    {
        cursor_texture = wlr_surface_get_texture(instance->client_cursor_surface);
        hotspot_x = instance->client_cursor_hotspot_x;
        hotspot_y = instance->client_cursor_hotspot_y;

        if (cursor_texture && !wlr_texture_is_gles2(cursor_texture))
        {
#ifdef DEBUG
            wlr_log(WLR_INFO, "Client cursor texture invalide (non GLES2 après "
                              "resize ?) → détacher et fallback");
#endif
            // DÉTACHE IMMÉDIATEMENT le curseur client
            if ((instance->client_cursor_destroy.link.next != nullptr) &&
                (instance->client_cursor_destroy.link.prev != nullptr))
            {
                wl_list_remove(&instance->client_cursor_destroy.link);
                wl_list_init(&instance->client_cursor_destroy.link);
            }

            instance->client_cursor_surface = nullptr;

            // Reset curseur vers système (flèche par défaut)
            if (instance->cursor_mgr)
            {
                wlr_cursor_set_xcursor(instance->cursor, instance->cursor_mgr,
                    "left_ptr");
            }

            cursor_texture = nullptr; // on n'utilise pas ce frame
        }
    }

    // Fall back to xcursor (toujours sûr)
    if ((cursor_texture == nullptr) && (instance->cursor_mgr != nullptr))
    {
        std::string target_xcursor = instance->current_xcursor_name;
        if (target_xcursor.empty())
        {
            target_xcursor = "left_ptr";
        }

        const struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(
            instance->cursor_mgr, target_xcursor.c_str(), 1);
        if (xcursor == nullptr)
        {
            target_xcursor = "left_ptr";
            xcursor =
                wlr_xcursor_manager_get_xcursor(instance->cursor_mgr, "left_ptr", 1);
        }

        if ((xcursor != nullptr) && (xcursor->image_count > 0))
        {
            const struct wlr_xcursor_image *image = xcursor->images[0];
            hotspot_x = image->hotspot_x;
            hotspot_y = image->hotspot_y;

            // Create or reuse cached texture for this xcursor
            if ((instance->xcursor_texture == nullptr) ||
                (instance->xcursor_texture_name == "") ||
                (instance->xcursor_texture_name != target_xcursor))
            {
                // Destroy old texture
                if (instance->xcursor_texture != nullptr)
                {
                    wlr_texture_destroy(instance->xcursor_texture);
                    instance->xcursor_texture = nullptr;
                }

                // free(instance->xcursor_texture_name);
                instance->xcursor_texture_name = "";

                // Create texture from xcursor image
                instance->xcursor_texture = wlr_texture_from_pixels(
                    instance->renderer, DRM_FORMAT_ARGB8888, image->width * 4,
                    image->width, image->height, image->buffer);

                if (instance->xcursor_texture != nullptr)
                {
                    instance->xcursor_texture_name = target_xcursor;
                }
            }

            cursor_texture = instance->xcursor_texture;
        }
    }

    if (cursor_texture == nullptr)
    {
        return;
    }

    // Apply viewport offset for multi-monitor: cursor position is in global
    // layout coords
    const int cursor_x = (int)instance->cursor->x - hotspot_x - viewport->x;
    const int cursor_y = (int)instance->cursor->y - hotspot_y - viewport->y;

    int cursor_w = static_cast<int>(cursor_texture->width);
    int cursor_h = static_cast<int>(cursor_texture->height);
    if ((instance->client_cursor_surface != nullptr) &&
        instance->client_cursor_surface->mapped &&
        (instance->client_cursor_surface->current.width > 0) &&
        (instance->client_cursor_surface->current.height > 0))
    {
        cursor_w = instance->client_cursor_surface->current.width;
        cursor_h = instance->client_cursor_surface->current.height;
    }

    struct wlr_box cursor_box = {
        .x     = cursor_x,
        .y     = cursor_y,
        .width = cursor_w,
        .height = cursor_h,
    };
    if (viewport->transform != WL_OUTPUT_TRANSFORM_NORMAL)
    {
        wlr_box_transform(&cursor_box, &cursor_box, viewport->transform,
            viewport->width, viewport->height);
    }

    struct wlr_render_texture_options opts = {
        .texture   = cursor_texture,
        .dst_box   = cursor_box,
        .transform = viewport->transform,
    };
    wlr_render_pass_add_texture(render_pass, &opts);
}

static void send_presentation_iterator(struct wlr_surface *surface, int sx,
    int sy, void *data)
{
    struct wlr_output *wlr_output = static_cast<struct wlr_output*>(data);
    if ((wlr_output != nullptr) && (surface != nullptr) && (surface->buffer != nullptr))
    {
        wlr_presentation_surface_textured_on_output(surface, wlr_output);
    }
}

static void send_frame_done_iterator(struct wlr_surface *surface, int sx,
    int sy, void *data)
{
    struct timespec *now = static_cast<struct timespec*>(data);
    wlr_surface_send_frame_done(surface, now);
}

static SparrowView *sparrow_output_find_fullscreen_candidate(Output *output)
{
    Core *instance = Core::instance();
    if (!output || !output->wlr_output || !output->wlr_output->enabled)
    {
        return nullptr;
    }

    // If Overview (force_render_all_views) or Gestures are active, direct mode must yield to Flutter
    // compositing
    if (instance->force_render_all_views || instance->gesture_active)
    {
        return nullptr;
    }

    // If popups or menus are active, direct mode must yield to Flutter compositing
    if (instance->popups && !instance->popups->empty())
    {
        for (const auto &[handle, value] : *instance->popups)
        {
            auto *popup = static_cast<SparrowPopup*>(value);
            if (popup && popup->xdg_surface && popup->xdg_surface->surface &&
                popup->xdg_surface->surface->mapped)
            {
                return nullptr;
            }
        }
    }

    // If Flutter is actively animating or updating its scene, yield to Flutter compositing
    if (instance->sparrow_renderer.current_scene.needs_update)
    {
        return nullptr;
    }

    SparrowView *candidate = nullptr;
    SparrowView *view = nullptr;
    wl_list_for_each(view, &instance->views_list, link)
    {
        if (!view || !view->xdg_surface || !view->xdg_surface->surface)
        {
            continue;
        }

        if (!view->xdg_surface->surface->mapped)
        {
            continue;
        }

        if ((view->current_output != nullptr) && (view->current_output != output))
        {
            continue;
        }

        // Only an ACTIVATED, FULLSCREEN window WITHOUT SSD titlebar/decorations can be a direct candidate
        if (view->fullscreen && view->activated && !view->uses_ssd)
        {
            struct wlr_buffer *target_buf = view->locked_buffer;
            SparrowSubSurface *sub = nullptr;
            wl_list_for_each(sub, &view->subsurfaces, link)
            {
                if (sub && sub->surface && sub->surface->mapped && (sub->locked_buffer != nullptr))
                {
                    target_buf = sub->locked_buffer;
                    break;
                }
            }

            if (target_buf != nullptr)
            {
                candidate = view;
            }
        }
    }

    return candidate;
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

static void prepare_fps_osd(Output *output, int out_w, int out_h, int damage_rect_count, uint64_t now_us)
{
    if (!output)
    {
        return;
    }

    Core *instance = Core::instance();
    if (!instance || !instance->renderer)
    {
        return;
    }

    double client_fps = instance->get_client_fps(now_us);

    const char *buf_tag = "";
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

    const char *scanout_tag = output->direct_mode_active ? " | DS:ON" : " | DS:OFF";

    char text[96];
    if (client_fps > 0.0)
    {
        if (damage_rect_count > 0)
        {
            snprintf(text, sizeof(text), "%.1f FPS | %.1fms | D:%d%s%s",
                client_fps, output->smoothed_render_ms, damage_rect_count, buf_tag, scanout_tag);
        } else
        {
            snprintf(text, sizeof(text), "%.1f FPS | %.1fms%s%s",
                client_fps, output->smoothed_render_ms, buf_tag, scanout_tag);
        }
    } else
    {
        snprintf(text, sizeof(text), "0.0 FPS | 0.0ms%s%s", buf_tag, scanout_tag);
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

    // Re-generate texture only if text changed or texture is null
    if ((output->osd_texture == nullptr) || (strcmp(output->last_osd_text, text) != 0))
    {
        if (output->osd_texture != nullptr)
        {
            wlr_texture_destroy(output->osd_texture);
            output->osd_texture = nullptr;
        }

        strncpy(output->last_osd_text, text, sizeof(output->last_osd_text) - 1);

        std::vector<uint32_t> pixels(box_w * box_h);
        uint32_t bg_color     = 0xD80D141F; // Dark translucent pill
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

        output->osd_texture = wlr_texture_from_pixels(
            instance->renderer, DRM_FORMAT_ARGB8888, box_w * 4, box_w, box_h, pixels.data());
    }
}

static void render_fps_osd(struct wlr_render_pass *render_pass, Output *output,
    int out_w, int out_h)
{
    if (!output || !render_pass || !output->osd_texture)
    {
        return;
    }

    int box_w = output->osd_texture->width;
    int box_h = output->osd_texture->height;
    int box_x = out_w - box_w - 16;
    int box_y = 16;

    struct wlr_render_texture_options opts = {
        .texture   = output->osd_texture,
        .dst_box   = {
            .x     = box_x,
            .y     = box_y,
            .width = box_w,
            .height = box_h,
        },
    };
    wlr_render_pass_add_texture(render_pass, &opts);
}

static void output_frame(struct wl_listener *listener, void *data)
{
    SPARROW_TRACE_SCOPE("Output::output_frame");
    Output *output = wl_container_of(listener, output, frame);
    Core *instance = Core::instance();
    struct wlr_output *wlr_output = output->wlr_output;

    struct timespec t_frame_start;
    clock_gettime(CLOCK_MONOTONIC, &t_frame_start);
    uint64_t now_us = (uint64_t)t_frame_start.tv_sec * 1000000ULL + (t_frame_start.tv_nsec / 1000);

    pthread_mutex_lock(&output->damage_mutex);
    bool needs_render = pixman_region32_not_empty(&output->damage_ring.current) ||
        instance->show_fps ||
        instance->sparrow_renderer.current_scene.needs_update;

    if (!needs_render)
    {
        pthread_mutex_unlock(&output->damage_mutex);
        if (output == instance->vsync_output)
        {
            intptr_t baton = atomic_exchange(&instance->vsync_baton, 0);
            if (baton != 0)
            {
                uint64_t current_time    = instance->embedder_api.GetCurrentTime();
                uint64_t frame_target_ns = 16600000;
                if (wlr_output->current_mode)
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

        // Still send frame done to surfaces on this output waiting for frame callback so they don't stall
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        SparrowView *view = nullptr;
        wl_list_for_each(view, &instance->views_list, link)
        {
            if (view && (view->xdg_surface != nullptr) &&
                (view->xdg_surface->surface != nullptr) &&
                view->xdg_surface->surface->mapped &&
                ((view->current_output == output) || (view->current_output == nullptr)))
            {
                wlr_surface_for_each_surface(view->xdg_surface->surface,
                    send_frame_done_iterator, &now);
            }
        }

        if (instance->popups)
        {
            for (const auto &[handle, value] : *instance->popups)
            {
                auto *popup = static_cast<SparrowPopup*>(value);
                if ((popup != nullptr) && (popup->xdg_surface != nullptr) &&
                    (popup->xdg_surface->surface != nullptr) &&
                    popup->xdg_surface->surface->mapped)
                {
                    wlr_surface_for_each_surface(popup->xdg_surface->surface,
                        send_frame_done_iterator, &now);
                }
            }
        }

        return;
    }

    // Atomically snapshot and clear current damage under lock
    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);
    pixman_region32_copy(&frame_damage, &output->damage_ring.current);
    pixman_region32_clear(&output->damage_ring.current);

    pixman_region32_t vis_damage;
    pixman_region32_init(&vis_damage);
    pixman_region32_copy(&vis_damage, &output->client_damage);
    pixman_region32_clear(&output->client_damage);
    pthread_mutex_unlock(&output->damage_mutex);

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

    // If show_fps is active, prepare texture before begin_render_pass and un-scissor in render pass
    if (instance->show_fps)
    {
        int damage_count = 0;
        if (instance->debug_damage && pixman_region32_not_empty(&vis_damage))
        {
            pixman_region32_rectangles(&vis_damage, &damage_count);
        }

        prepare_fps_osd(output, wlr_output->width, wlr_output->height, damage_count, now_us);
        pixman_region32_union_rect(&render_damage, &render_damage,
            wlr_output->width - 420, 0, 420, 60);
    }

    // Direct Scanout / Direct Draw check for fullscreen applications / games
    SparrowView *fs_view = sparrow_output_find_fullscreen_candidate(output);
    struct wlr_buffer *target_buf = nullptr;
    struct wlr_surface *target_surface = nullptr;
    if (fs_view != nullptr)
    {
        target_buf     = fs_view->locked_buffer;
        target_surface = fs_view->xdg_surface ? fs_view->xdg_surface->surface : nullptr;
        SparrowSubSurface *sub = nullptr;
        wl_list_for_each(sub, &fs_view->subsurfaces, link)
        {
            if (sub && sub->surface && sub->surface->mapped && (sub->locked_buffer != nullptr))
            {
                target_buf     = sub->locked_buffer;
                target_surface = sub->surface;
                break;
            }
        }
    }

    if ((fs_view != nullptr) && (target_buf != nullptr) && (target_surface != nullptr))
    {
        double refresh_budget_ms = (wlr_output->current_mode && wlr_output->current_mode->refresh > 0) ?
            (1000000.0 / wlr_output->current_mode->refresh) :
            16.666;

        // DIRECT 1-PASS GPU DRAW / BLIT (0.1ms GPU render, zero latency, flicker-free with overview &
        // gestures)
        struct wlr_output_state direct_blit_state;
        wlr_output_state_init(&direct_blit_state);
        wlr_output_state_set_damage(&direct_blit_state, &render_damage);

        struct wlr_render_pass *direct_pass =
            wlr_output_begin_render_pass(wlr_output, &direct_blit_state, nullptr);
        if (direct_pass != nullptr)
        {
            struct wlr_texture *tex =
                wlr_texture_from_buffer(instance->renderer, target_buf);
            if (tex != nullptr)
            {
                struct wlr_box dst_box = {
                    .x     = 0,
                    .y     = 0,
                    .width = wlr_output->width,
                    .height = wlr_output->height,
                };
                struct wlr_render_texture_options opts = {
                    .texture   = tex,
                    .dst_box   = dst_box,
                    .transform = wlr_output->transform,
                };
                wlr_render_pass_add_texture(direct_pass, &opts);
                wlr_texture_destroy(tex);

                if (instance->show_fps)
                {
                    SPARROW_GL_SCOPE("Sparrow::FPS_OSD");
                    render_fps_osd(direct_pass, output, wlr_output->width, wlr_output->height);
                }

                bool submit_ok = wlr_render_pass_submit(direct_pass);
                if (submit_ok)
                {
                    wlr_surface_for_each_surface(fs_view->xdg_surface->surface,
                        send_presentation_iterator, wlr_output);
                    wlr_output_commit_state(wlr_output, &direct_blit_state);
                    wlr_output_state_finish(&direct_blit_state);

                    output->direct_draw_flips++;
                    output->direct_mode_active = true;
                    fs_view->current_buffer_sampled = true;
                    fs_view->sampled_count++;

                    output->flip_count++;
                    output->last_flip_time_us = now_us;

                    if (instance->debug_pacing && (now_us - output->last_pacing_log_time_us >= 2000000ULL))
                    {
                        output->last_pacing_log_time_us = now_us;
                        const char *app = (fs_view->toplevel &&
                            fs_view->toplevel->app_id) ? fs_view->toplevel->app_id : "app";
                        wlr_log(WLR_INFO,
                            "[PACING-DIRECT-BLIT] Output '%s': App '%s' Direct 1-Pass Blit Active | Flips=%lu (Direct 1-Pass=%lu, Composited=%lu)",
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
                            uint64_t frame_target_ns = (uint64_t)(refresh_budget_ms * 1000000.0);
                            instance->embedder_api.OnVsync(instance->engine, baton, current_time,
                                current_time + frame_target_ns);
                        }
                    }

                    struct timespec ts_done;
                    clock_gettime(CLOCK_MONOTONIC, &ts_done);
                    wlr_surface_for_each_surface(fs_view->xdg_surface->surface,
                        send_frame_done_iterator, &ts_done);

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
    output->direct_mode_active = false;

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
    pthread_mutex_lock(&instance->sparrow_renderer.render_mutex);
    has_scene_layers = (instance->sparrow_renderer.current_scene.layers_count > 0);
    pthread_mutex_unlock(&instance->sparrow_renderer.render_mutex);

    if (!has_scene_layers)
    {
        struct wlr_render_rect_options opts = {
            .box   = {.x = 0,
                .y = 0,
                .width  = wlr_output->width,
                .height = wlr_output->height},
            .color = {0.1f, 0.1f, 0.1f, 1.0f},
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

    pthread_mutex_lock(&instance->sparrow_renderer.render_mutex);

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

    // Live damage visualization: highlight client damaged rectangles with thick 4px borders
    if (instance->debug_damage && pixman_region32_not_empty(&vis_damage))
    {
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
            pixman_region32_rectangles(&vis_damage, &nrects);

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

            const int bw = (rw >= 24 && rh >= 24) ? 4 : ((rw >= 12 && rh >= 12) ? 3 : 2);

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

    // Render software cursor on top of everything
    render_cursor(render_pass, &viewport);

    // Render FPS & latency debug OSD if enabled
    struct timespec t_render_now;
    clock_gettime(CLOCK_MONOTONIC, &t_render_now);
    double cur_render_ms = ((t_render_now.tv_sec - t_frame_start.tv_sec) * 1000.0) +
        ((t_render_now.tv_nsec - t_frame_start.tv_nsec) / 1000000.0);
    output->smoothed_render_ms = (output->smoothed_render_ms <= 0.0) ?
        cur_render_ms :
        (output->smoothed_render_ms * 0.9 + cur_render_ms * 0.1);

    double refresh_budget_ms = (wlr_output->current_mode && wlr_output->current_mode->refresh > 0) ?
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
        render_fps_osd(render_pass, output, wlr_output->width, wlr_output->height);
    }

    pixman_region32_fini(&render_damage);
    pixman_region32_fini(&frame_damage);
    pixman_region32_fini(&vis_damage);

    // Submit render pass
    bool submit_ok = wlr_render_pass_submit(render_pass);

    pthread_mutex_unlock(&instance->sparrow_renderer.render_mutex);

    if (!submit_ok)
    {
        wlr_output_state_finish(&output_state);
        wlr_output_schedule_frame(wlr_output);
        return;
    }

    // Mark visible surfaces on THIS output for presentation feedback BEFORE output commit
    SparrowView *view = nullptr;
    wl_list_for_each(view, &instance->views_list, link)
    {
        if (view && (view->xdg_surface != nullptr) &&
            (view->xdg_surface->surface != nullptr) &&
            view->xdg_surface->surface->mapped &&
            ((view->current_output == output) || (view->current_output == nullptr)))
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
                popup->xdg_surface->surface->mapped)
            {
                wlr_surface_for_each_surface(popup->xdg_surface->surface,
                    send_presentation_iterator, wlr_output);
            }
        }
    }

    // Commit the output
    {
        SPARROW_TRACE_SCOPE("Output::CommitState");
        wlr_output_commit_state(wlr_output, &output_state);
        wlr_output_state_finish(&output_state);
    }

    output->flip_count++;
    double flip_dt_ms = (output->last_flip_time_us > 0) ?
        (double)(now_us - output->last_flip_time_us) / 1000.0 :
        0.0;
    output->last_flip_time_us = now_us;

    if ((flip_dt_ms > (1.35 * refresh_budget_ms)) && (output->flip_count > 10))
    {
        output->missed_vblank_count++;
        if (instance->debug_pacing)
        {
            wlr_log(WLR_INFO,
                "[PACING-VBLANK-MISS] Output '%s' frame interval spike: %.2fms (target: %.2fms, missed: %lu)",
                wlr_output->name, flip_dt_ms, refresh_budget_ms,
                (unsigned long)output->missed_vblank_count);
        }
    }

    // Periodic health summary log every 2 seconds if debug_pacing is active
    if (instance->debug_pacing && (now_us - output->last_pacing_log_time_us >= 2000000ULL))
    {
        output->last_pacing_log_time_us = now_us;
        SparrowView *active_view = nullptr;
        wl_list_for_each(active_view, &instance->views_list, link)
        {
            if (active_view && (active_view->commit_count > 0))
            {
                const char *app = (active_view->toplevel &&
                    active_view->toplevel->app_id) ? active_view->toplevel->app_id : "app";
                wlr_log(WLR_INFO,
                    "[PACING-SUMMARY] App '%s': Commits=%lu | Sampled=%lu | DroppedOverwritten=%lu | GhostDupes=%lu || KMS: Flips=%lu | VBlankMisses=%lu",
                    app,
                    (unsigned long)active_view->commit_count,
                    (unsigned long)active_view->sampled_count,
                    (unsigned long)active_view->dropped_buffer_count,
                    (unsigned long)active_view->duplicate_sample_count,
                    (unsigned long)output->flip_count,
                    (unsigned long)output->missed_vblank_count);
            }
        }
    }

    // Dispatch VSync baton for the designated vsync output immediately after KMS commit
    // so Flutter has the full upcoming refresh cycle budget to render the next frame on time.
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

    // Send frame done to Wayland surfaces that requested a frame callback on this output
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    wl_list_for_each(view, &instance->views_list, link)
    {
        if (view && (view->xdg_surface != nullptr) &&
            (view->xdg_surface->surface != nullptr) &&
            view->xdg_surface->surface->mapped &&
            ((view->current_output == output) || (view->current_output == nullptr)))
        {
            wlr_surface_for_each_surface(view->xdg_surface->surface,
                send_frame_done_iterator, &now);
        }
    }

    // Send frame done to popup surfaces too if they requested a frame callback
    if (instance->popups)
    {
        for (const auto &[handle, value] : *instance->popups)
        {
            auto *popup = static_cast<SparrowPopup*>(value);
            if ((popup != nullptr) && (popup->xdg_surface != nullptr) &&
                (popup->xdg_surface->surface != nullptr) &&
                popup->xdg_surface->surface->mapped)
            {
                wlr_surface_for_each_surface(popup->xdg_surface->surface,
                    send_frame_done_iterator, &now);
            }
        }
    }
}

static void output_request_state(struct wl_listener *listener, void *data)
{
    Output *output = wl_container_of(listener, output, request_state);
    struct wlr_output_event_request_state *event =
        static_cast<wlr_output_event_request_state*>(data);

    wlr_output_commit_state(output->wlr_output, event->state);

#ifdef DAMAGE_HISTORY
    sparrow_output_damage_history_reset(output, output->wlr_output->width,
        output->wlr_output->height);
#endif

    // Update Flutter window metrics with TOTAL layout bounds, not just this
    // output Using single-output dimensions here was causing severe rendering
    // bugs in multi-monitor setups
    Core *instance = Core::instance();
    if (instance->engine != nullptr)
    {
        struct wlr_box total_box = {};
        wlr_output_layout_get_box(instance->output_layout, nullptr, &total_box);

        if ((total_box.width > 0) && (total_box.height > 0))
        {
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

            SparrowView *view;
            wl_list_for_each(view, &instance->views_list, link)
            {
                if (view && (view->xdg_surface != nullptr) &&
                    (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) &&
                    (view->toplevel != nullptr) && view->maximized)
                {
                    Output *out = view->current_output ? view->current_output :
                        sparrow_get_first_output();
                    if (out && out->wlr_output)
                    {
                        int eff_w = 0, eff_h = 0;
                        wlr_output_effective_resolution(out->wlr_output, &eff_w, &eff_h);
                        wlr_xdg_toplevel_set_size(view->toplevel, eff_w, eff_h);
                    }
                }
            }
        }
    }
}

static void output_present(struct wl_listener *listener, void *data)
{
    Output *output = wl_container_of(listener, output, present);
    Core *instance = Core::instance();
    struct wlr_output_event_present *event =
        static_cast<wlr_output_event_present*>(data);

    // Only handle vsync baton for the designated vsync output
    if (output != instance->vsync_output)
    {
        return;
    }

    if (output->vsync_dispatched_in_frame)
    {
        // VSync was already dispatched in output_frame for this flip cycle (triple buffering)
        output->vsync_dispatched_in_frame = false;
        return;
    }

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
    Core *instance = Core::instance();

    atomic_store(&instance->vsync_baton, baton);

    instance->callable_queue.enqueue([instance] ()
    {
        if ((instance->vsync_output != nullptr) &&
            (instance->vsync_output->wlr_output != nullptr) &&
            instance->vsync_output->wlr_output->enabled)
        {
            wlr_output_schedule_frame(instance->vsync_output->wlr_output);
        } else
        {
            Output *out = nullptr;
            wl_list_for_each(out, &instance->outputs, link)
            {
                if (out && out->wlr_output && out->wlr_output->enabled)
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
    Core *instance = Core::instance();
    if (instance == nullptr)
    {
        return;
    }

    const uint32_t output_id    = output->id;
    const bool was_vsync_output = (output == instance->vsync_output);

    wlr_log(WLR_INFO, "Output %s (id=%d) disconnected",
        output->wlr_output ? output->wlr_output->name : "unknow",
        output_id);

    // Remove from output layout FIRST so it doesn't query a destroyed output
    if ((instance->output_layout != nullptr) && (output->wlr_output != nullptr))
    {
        wlr_output_layout_remove(instance->output_layout, output->wlr_output);
    }

    // Remove listeners safely
    if ((output->frame.link.prev != nullptr) && (output->frame.link.next != nullptr))
    {
        wl_list_remove(&output->frame.link);
        wl_list_init(&output->frame.link);
    }

    if ((output->request_state.link.prev != nullptr) && (output->request_state.link.next != nullptr))
    {
        wl_list_remove(&output->request_state.link);
        wl_list_init(&output->request_state.link);
    }

    if ((output->present.link.prev != nullptr) && (output->present.link.next != nullptr))
    {
        wl_list_remove(&output->present.link);
        wl_list_init(&output->present.link);
    }

    if ((output->destroy.link.prev != nullptr) && (output->destroy.link.next != nullptr))
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

    wlr_damage_ring_finish(&output->damage_ring);
    pixman_region32_fini(&output->client_damage);
    pthread_mutex_destroy(&output->damage_mutex);
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
    }
}

void sparrow_server_new_output(struct wl_listener *listener, void *data)
{
    (void)listener;
    Core *instance = Core::instance();
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
    pthread_mutex_init(&output->damage_mutex, nullptr);
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
        "[TRIPLE_BUFFERING] Output %s: Triple Buffering active (swapchain capacity=%d, damage history ring=%d)",
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
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

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
    wlr_output_state_set_adaptive_sync_enabled(&state, true);
    if (!wlr_output_test_state(wlr_output, &state))
    {
        wlr_output_state_set_adaptive_sync_enabled(&state, false);
    } else
    {
        wlr_log(WLR_INFO, "Output %s: Adaptive Sync (VRR) enabled", wlr_output->name);
    }

    if (!wlr_output_commit_state(wlr_output, &state))
    {
        wlr_log(WLR_ERROR, "Failed to commit output state for %s",
            wlr_output->name);
        wlr_output_state_finish(&state);
        // Clean up on failure
        wl_list_remove(&output->frame.link);
        wl_list_remove(&output->request_state.link);
        wl_list_remove(&output->present.link);
        wl_list_remove(&output->destroy.link);
        wl_list_remove(&output->link);
        return;
    }

    wlr_output_state_finish(&state);

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
}

// Helper to get first output from list (for backwards compatibility)
// Returns nullptr if no outputs are connected
Output *sparrow_get_first_output()
{
    Core *instance = Core::instance();

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
    Core *instance = Core::instance();

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
        if (state->adaptive_sync_enabled == current_adaptive)
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
    Core *instance = Core::instance();
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
            wl_list_for_each(output, &instance->outputs, link)
            {
                send_output_changed(output);
            }

            SparrowView *view;
            wl_list_for_each(view, &instance->views_list, link)
            {
                if (view && (view->xdg_surface != nullptr) &&
                    (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) &&
                    (view->toplevel != nullptr) && view->maximized)
                {
                    Output *out = view->current_output ? view->current_output :
                        sparrow_get_first_output();
                    if (out && out->wlr_output)
                    {
                        int eff_w = 0, eff_h = 0;
                        wlr_output_effective_resolution(out->wlr_output, &eff_w, &eff_h);
                        wlr_xdg_toplevel_set_size(view->toplevel, eff_w, eff_h);
                    }
                }
            }
        }
    }
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
    Core *instance = Core::instance();

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

static void handle_output_power_manager_set_mode(struct wl_listener *listener, void *data)
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
        wlr_log(WLR_INFO, "[DPMS] Output '%s' power mode: OFF", event->output->name);
        break;

      case ZWLR_OUTPUT_POWER_V1_MODE_ON:
        wlr_output_state_set_enabled(&state, true);
        wlr_log(WLR_INFO, "[DPMS] Output '%s' power mode: ON", event->output->name);
        break;
    }

    if (!wlr_output_commit_state(event->output, &state))
    {
        wlr_log(WLR_ERROR, "[DPMS] Failed to commit power state for output '%s'", event->output->name);
    } else
    {
        if (event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON)
        {
            sparrow_select_highest_refresh_output();
            sparrow_damage_add_box(nullptr);
            wlr_output_schedule_frame(event->output);
        }
    }

    wlr_output_state_finish(&state);
}

void sparrow_output_power_manager_init()
{
    Core *instance = Core::instance();

    instance->output_power_manager = wlr_output_power_manager_v1_create(instance->wl_display);
    if (instance->output_power_manager == nullptr)
    {
        wlr_log(WLR_ERROR, "Failed to create wlr_output_power_manager_v1");
        return;
    }

    instance->output_power_manager_set_mode.notify = handle_output_power_manager_set_mode;
    wl_signal_add(&instance->output_power_manager->events.set_mode,
        &instance->output_power_manager_set_mode);

    wlr_log(WLR_INFO, "Initialized wlr_output_power_management_v1 (DPMS power control)");
}
