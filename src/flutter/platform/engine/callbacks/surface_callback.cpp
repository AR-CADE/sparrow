#include "surface_callback.hpp"
#include "output.hpp"
#include "util/handle_map.hpp"
#include <core.hpp>
#include <flutter/platform/engine/messages/surface_message.hpp>
#include <surface/popup.hpp>
#include <surface/session_lock.hpp>
#include <surface/sub_surface.hpp>
#include <surface/view.hpp>
void handle_foreign_activate_request(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowView *view = wl_container_of(listener, view, foreign_activate_request);
    wlr_log(WLR_INFO, "Foreign toplevel requested activate for view %d",
        view->handle);
    sparrow_view_focus(view);
    send_surface_request_activate(
        view->handle, "",
        view->toplevel && view->toplevel->app_id ? view->toplevel->app_id : "");
}

void handle_foreign_close_request(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowView *view = wl_container_of(listener, view, foreign_close_request);
    wlr_log(WLR_INFO, "Foreign toplevel requested close for view %d",
        view->handle);
    if (view->toplevel)
    {
        wlr_xdg_toplevel_send_close(view->toplevel);
    }
}

void handle_foreign_maximize_request(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, foreign_maximize_request);
    auto *event =
        static_cast<struct wlr_foreign_toplevel_handle_v1_maximized_event*>(
            data);
    wlr_log(WLR_INFO, "Foreign toplevel requested maximize=%d for view %d",
        event->maximized, view->handle);
    if (view->toplevel && view->xdg_surface && view->xdg_surface->initialized)
    {
        if (view->fullscreen)
        {
            return;
        }

        view->maximized = true;
        wlr_xdg_toplevel_set_maximized(view->toplevel, true);
        if (view->foreign_toplevel)
        {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel,
                true);
        }
    }
}

void handle_foreign_minimize_request(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowView *view = wl_container_of(listener, view, foreign_minimize_request);
    wlr_log(WLR_INFO, "Foreign toplevel requested minimize for view %d",
        view->handle);
    send_surface_minimize(view);
}

void handle_foreign_fullscreen_request(struct wl_listener *listener,
    void *data)
{
    SparrowView *view =
        wl_container_of(listener, view, foreign_fullscreen_request);
    auto *event =
        static_cast<struct wlr_foreign_toplevel_handle_v1_fullscreen_event*>(
            data);
    wlr_log(WLR_INFO, "Foreign toplevel requested fullscreen=%d for view %d",
        event->fullscreen, view->handle);
    if (view && view->toplevel)
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const uint64_t now_ms =
            (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);

        sparrow_view_cancel_pending_unfullscreen(view);
        view->fullscreen = event->fullscreen;
        view->maximized  = !event->fullscreen;
        view->fullscreen_active_since_ms = 0;
        view->fullscreen_requested_ms    = event->fullscreen ? now_ms : 0;
        if (view->xdg_surface && view->xdg_surface->initialized)
        {
            wlr_xdg_toplevel_set_maximized(view->toplevel, !event->fullscreen);
            wlr_xdg_toplevel_set_fullscreen(view->toplevel, event->fullscreen);
        }

        if (view->foreign_toplevel)
        {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel,
                !event->fullscreen);
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel,
                view->fullscreen);
        }

        send_surface_fullscreen(view, event->fullscreen);
        sparrow_update_idle_inhibited();
    }
}

void sparrow_handle_surface_focus(uint32_t surface_handle)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view || !view->xdg_surface || !view->xdg_surface->surface ||
        !view->xdg_surface->surface->mapped || !view->xdg_surface->initialized)
    {
        return;
    }

    sparrow_view_focus(view);
    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }

    if (view->toplevel)
    {
        wlr_xdg_surface_schedule_configure(view->xdg_surface);
    }
}

void sparrow_handle_surface_set_position(uint32_t surface_handle, int x,
    int y)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view)
    {
        return;
    }

    const bool pos_changed = (view->x != x) || (view->y != y);
    if (!pos_changed)
    {
        return;
    }

    // Update position (Dart is source of truth for window positioning)
    // Note: position from Dart includes the decoration/titlebar
    view->x = x;
    view->y = y;

    // Update output tracking for multi-monitor scale
    Output *new_output =
        sparrow_output_for_box(view->x, view->y, view->width, view->height);
    const bool output_changed = (view->current_output != new_output);

    if (output_changed)
    {
        view->current_output = new_output;
        view->output_scale   = new_output ? new_output->wlr_output->scale : 1.0;

        // Also update child subsurfaces
        SparrowSubSurface *sub;
        wl_list_for_each(sub, &view->subsurfaces, link)
        {
            sub->current_output = new_output;
            sub->output_scale   = view->output_scale;
        }

        wlr_log(WLR_DEBUG, "View %d output changed: %s (scale=%.2f)", view->handle,
            new_output ? new_output->wlr_output->name : "none",
            view->output_scale);
    }

    // Also update and re-unconstrain child popups if window moved or output
    // changed
    if ((pos_changed || output_changed) && instance->popups)
    {
        for (const auto &[popup_handle, popup_value] : *instance->popups)
        {
            auto *popup = static_cast<SparrowPopup*>(popup_value);
            if ((popup != nullptr) && (popup->parent_view == view))
            {
                popup->current_output = view->current_output;
                popup->output_scale   = view->output_scale;
                sparrow_popup_unconstrain(popup);
                wlr_log(WLR_DEBUG, "Popup %d updated (parent view %d moved to %d,%d)",
                    popup->handle, view->handle, view->x, view->y);
            }
        }
    }

    // Update scene tree position for input hit-testing
    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }
}

void sparrow_handle_surface_set_scene_bounds(uint32_t surface_handle, int x,
    int y, int width, int height,
    double corner_radius,
    bool visible)
{
    const auto instance = Core::instance();
    if (!instance)
    {
        return;
    }

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view)
    {
        return;
    }

    const bool new_visible  = visible && (width > 0) && (height > 0);
    const double new_radius = (corner_radius > 0.0) ? corner_radius : 0.0;
    const bool changed =
        !view->has_flutter_scene_box || (view->scene_visible != new_visible) ||
        (view->flutter_scene_box.x != x) || (view->flutter_scene_box.y != y) ||
        (view->flutter_scene_box.width != width) ||
        (view->flutter_scene_box.height != height) ||
        (std::abs(view->flutter_corner_radius - new_radius) >= 0.1);

    view->has_flutter_scene_box = true;
    view->scene_visible = new_visible;
    if ((width > 0) && (height > 0))
    {
        view->flutter_scene_box = {
            .x     = x,
            .y     = y,
            .width = width,
            .height = height,
        };
        view->flutter_corner_radius = new_radius;
    }

    if (changed && view->is_sensitive)
    {
        sparrow_damage_add_box(nullptr);
    }
}

bool sparrow_handle_surface_toplevel_close(uint32_t surface_handle)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view)
    {
        return true;
    }

    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    {
        wlr_xdg_toplevel_send_close(view->toplevel);
    }

    return true;
}

void sparrow_handle_surface_toplevel_set_maximized(uint32_t surface_handle,
    bool maximized)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view || !view->xdg_surface || !view->xdg_surface->initialized ||
        !view->toplevel)
    {
        return;
    }

    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    {
        if (maximized && view->fullscreen)
        {
            return;
        }

        if ((view->maximized == maximized) &&
            (view->toplevel->scheduled.maximized == maximized))
        {
            return;
        }

        view->maximized = maximized;
        Output *out = view->current_output ? view->current_output :
            sparrow_get_first_output();
        int eff_w = 0;
        int eff_h = 0;
        if (out && out->wlr_output)
        {
            wlr_output_effective_resolution(out->wlr_output, &eff_w, &eff_h);
            const bool is_wleird =
                (!view->toplevel->app_id || view->toplevel->app_id[0] == '\0');
            if (is_wleird && (eff_h > eff_w))
            {
                eff_h -= 1;
            }
        }

        if (maximized)
        {
            view->pending_width  = eff_w;
            view->pending_height = eff_h;
            wlr_xdg_toplevel_set_size(view->toplevel, eff_w, eff_h);
        }

        wlr_xdg_toplevel_set_maximized(view->toplevel, maximized);

        if (view->foreign_toplevel)
        {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel,
                maximized);
        }

        wlr_xdg_surface_schedule_configure(view->xdg_surface);
    }
}

void sparrow_handle_surface_toplevel_set_fullscreen(uint32_t surface_handle,
    bool fullscreen)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view || !view->xdg_surface || !view->xdg_surface->initialized ||
        !view->toplevel)
    {
        return;
    }

    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    {
        sparrow_view_cancel_pending_unfullscreen(view);
        if ((view->fullscreen == fullscreen) &&
            (view->toplevel->scheduled.fullscreen == fullscreen))
        {
            send_surface_fullscreen(view, fullscreen);
            return;
        }

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const uint64_t now_ms =
            (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);

        bool was_fullscreen = view->fullscreen;
        view->fullscreen = fullscreen;
        view->maximized  = !fullscreen;
        view->fullscreen_active_since_ms = 0;
        view->fullscreen_requested_ms    = fullscreen ? now_ms : 0;
        Output *out = view->current_output ? view->current_output :
            sparrow_get_first_output();
        int eff_w = 0;
        int eff_h = 0;
        if (out && out->wlr_output)
        {
            wlr_output_effective_resolution(out->wlr_output, &eff_w, &eff_h);
        }

        if ((view->pending_width != eff_w) || (view->pending_height != eff_h) ||
            (was_fullscreen != fullscreen))
        {
            view->pending_width  = eff_w;
            view->pending_height = eff_h;
            wlr_xdg_toplevel_set_size(view->toplevel, eff_w, eff_h);
        }

        if (view->toplevel->scheduled.fullscreen != fullscreen)
        {
            wlr_xdg_toplevel_set_fullscreen(view->toplevel, fullscreen);
        }

        if (view->toplevel->scheduled.maximized != !fullscreen)
        {
            wlr_xdg_toplevel_set_maximized(view->toplevel, !fullscreen);
        }

        if (view->foreign_toplevel)
        {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel,
                fullscreen);
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel,
                !fullscreen);
        }

        wlr_xdg_surface_schedule_configure(view->xdg_surface);
        send_surface_fullscreen(view, fullscreen);
        if (fullscreen)
        {
            sparrow_view_focus(view);
        }

        sparrow_update_idle_inhibited();
    }
}

void sparrow_handle_surface_toplevel_set_size(uint32_t surface_handle,
    int width, int height)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view || !view->xdg_surface || !view->xdg_surface->initialized ||
        !view->toplevel)
    {
        return;
    }

    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    {
        view->pending_width  = width;
        view->pending_height = height;
        wlr_xdg_toplevel_set_size(view->toplevel, width, height);
        wlr_xdg_surface_schedule_configure(view->xdg_surface);
    }
}

void sparrow_handle_surface_request_resize(uint32_t surface_handle, int width,
    int height, uint64_t request_id)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view || !view->xdg_surface || !view->xdg_surface->initialized ||
        !view->toplevel)
    {
        return;
    }

    wlr_log(WLR_INFO, "Request resize: handle=%d, size=%dx%d, request_id=%lu",
        surface_handle, width, height, request_id);

    // Store pending resize state
    view->resize_in_progress = true;
    view->pending_width     = width;
    view->pending_height    = height;
    view->resize_request_id = request_id;

    // Tell the client it's being resized (affects some client behavior)
    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    {
        wlr_xdg_toplevel_set_resizing(view->toplevel, true);
        // Send configure with new size
        wlr_xdg_toplevel_set_size(view->toplevel, width, height);
        wlr_xdg_surface_schedule_configure(view->xdg_surface);
    }
}

void sparrow_handle_surface_end_resize(uint32_t surface_handle)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view || !view->xdg_surface || !view->xdg_surface->initialized ||
        !view->toplevel)
    {
        return;
    }

    wlr_log(WLR_INFO, "End resize: handle=%d", surface_handle);

    // Clear resize state
    view->resize_in_progress = false;
    view->pending_width     = 0;
    view->pending_height    = 0;
    view->resize_request_id = 0;

    // Tell the client resize is done
    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    {
        wlr_xdg_toplevel_set_resizing(view->toplevel, false);
        wlr_xdg_surface_schedule_configure(view->xdg_surface);
    }
}
