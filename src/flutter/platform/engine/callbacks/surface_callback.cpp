#include "flutter_embedder.h"
#include <core.hpp>
#include <surface/view.hpp>
#include <surface/sub_surface.hpp>
#include <surface/popup.hpp>
#include <flutter/platform/engine/messages/surface_message.hpp>
#include "flutter/platform/messages.hpp"
#include "surface_callback.hpp"

void handle_foreign_activate_request(struct wl_listener *listener,
    void *data)
{
    (void)data;
    SparrowView *view = wl_container_of(listener, view, foreign_activate_request);
    wlr_log(WLR_INFO, "Foreign toplevel requested activate for view %d", view->handle);
    sparrow_view_focus(view);
    send_surface_request_activate(view->handle, "",
        view->toplevel && view->toplevel->app_id ?
        view->toplevel->app_id :
        "");
}

void handle_foreign_close_request(struct wl_listener *listener,
    void *data)
{
    (void)data;
    SparrowView *view = wl_container_of(listener, view, foreign_close_request);
    wlr_log(WLR_INFO, "Foreign toplevel requested close for view %d", view->handle);
    if (view->toplevel)
    {
        wlr_xdg_toplevel_send_close(view->toplevel);
    }
}

void handle_foreign_maximize_request(struct wl_listener *listener,
    void *data)
{
    SparrowView *view = wl_container_of(listener, view, foreign_maximize_request);
    auto *event =
        static_cast<struct wlr_foreign_toplevel_handle_v1_maximized_event*>(data);
    wlr_log(WLR_INFO, "Foreign toplevel requested maximize=%d for view %d",
        event->maximized, view->handle);
    if (view->toplevel)
    {
        view->maximized = event->maximized;
        wlr_xdg_toplevel_set_maximized(view->toplevel, event->maximized);
        if (view->foreign_toplevel)
        {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel,
                view->maximized);
        }
    }
}

void handle_foreign_minimize_request(struct wl_listener *listener,
    void *data)
{
    (void)data;
    SparrowView *view = wl_container_of(listener, view, foreign_minimize_request);
    wlr_log(WLR_INFO, "Foreign toplevel requested minimize for view %d", view->handle);
    send_surface_minimize(view);
}

void handle_foreign_fullscreen_request(struct wl_listener *listener,
    void *data)
{
    SparrowView *view = wl_container_of(listener, view, foreign_fullscreen_request);
    auto *event =
        static_cast<struct wlr_foreign_toplevel_handle_v1_fullscreen_event*>(data);
    wlr_log(WLR_INFO, "Foreign toplevel requested fullscreen=%d for view %d",
        event->fullscreen, view->handle);
    if (view && view->toplevel)
    {
        view->fullscreen = event->fullscreen;
        if (view->xdg_surface && view->xdg_surface->initialized)
        {
            wlr_xdg_toplevel_set_fullscreen(view->toplevel, event->fullscreen);
        }

        if (view->foreign_toplevel)
        {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel,
                view->fullscreen);
        }
    }
}

void sparrow_handle_surface_focus(uint32_t surface_handle)
{
    Core *instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view)
    {
        return;
    }

    sparrow_view_focus(view);
    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }
}

void sparrow_handle_surface_set_position(uint32_t surface_handle, int x, int y)
{
    Core *instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view)
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

    if (view->current_output != new_output)
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

        // Also update child popups (stored in instance->popups map)
        if (instance->popups)
        {
            for (const auto &[popup_handle, popup_value] : *instance->popups)
            {
                auto *popup = static_cast<SparrowPopup*>(popup_value);
                if ((popup != nullptr) && (popup->parent_view == view))
                {
                    popup->current_output = new_output;
                    popup->output_scale   = view->output_scale;
                    wlr_log(
                        WLR_DEBUG, "Popup %d output updated to %s (parent view %d moved)",
                        popup->handle, new_output ? new_output->wlr_output->name : "none",
                        view->handle);
                }
            }
        }

        wlr_log(WLR_DEBUG, "View %d output changed: %s (scale=%.2f)", view->handle,
            new_output ? new_output->wlr_output->name : "none",
            view->output_scale);
    }

    // Update scene tree position for input hit-testing
    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_set_position(&view->scene_tree->node, 0, 0);
    }
}

bool sparrow_handle_surface_toplevel_close(uint32_t surface_handle)
{
    Core *instance = Core::instance();

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

void sparrow_handle_surface_toplevel_set_maximized(uint32_t surface_handle, bool maximized)
{
    Core *instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view)
    {
        return;
    }

    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    {
        view->maximized = maximized;
        Output *out = view->current_output ? view->current_output :
            sparrow_get_first_output();
        int eff_w = 0;
        int eff_h = 0;
        if (out && out->wlr_output)
        {
            wlr_output_effective_resolution(out->wlr_output, &eff_w, &eff_h);
        }

        wlr_xdg_toplevel_set_size(view->toplevel, eff_w, eff_h);
        wlr_xdg_toplevel_set_maximized(view->toplevel, maximized);
    }
}

void sparrow_handle_surface_toplevel_set_size(uint32_t surface_handle, int width, int height)
{
    Core *instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view)
    {
        return;
    }

    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    {
        wlr_xdg_toplevel_set_size(view->toplevel, width, height);
    }
}

void sparrow_handle_surface_request_resize(uint32_t surface_handle, int width, int height,
    uint64_t request_id)
{
    Core *instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view)
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
    }

    // Send configure with new size
    wlr_xdg_toplevel_set_size(view->toplevel, width, height);
}

void sparrow_handle_surface_end_resize(uint32_t surface_handle)
{
    Core *instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (!view)
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
    }
}
