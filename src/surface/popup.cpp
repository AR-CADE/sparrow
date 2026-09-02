#include "flutter_embedder.h"

#include "core.hpp"
#include "flutter/platform/engine/messages/popup_message.hpp"
#include "view.hpp"
#include "popup.hpp"



void sparrow_popup_damage_whole(SparrowPopup *popup)
{
    if (!popup)
    {
        return;
    }

    Core *instance = Core::instance();
    if (instance && instance->force_render_all_views)
    {
        sparrow_damage_add_box(nullptr, false);
        return;
    }

    SparrowView *view = popup->parent_view;
    Output *output    = (view && view->current_output) ? view->current_output : sparrow_get_first_output();
    if (!output || !output->wlr_output)
    {
        return;
    }

    const int out_w = output->wlr_output->width;
    const int out_h = output->wlr_output->height;

    const int vis_w = (view && view->width > 0) ? view->width : out_w;
    const int vis_h = (view && view->height > 0) ? view->height : out_h;
    const double scale_x = (vis_w > 0) ? ((double)out_w / (double)vis_w) : 1.0;
    const double scale_y = (vis_h > 0) ? ((double)out_h / (double)vis_h) : 1.0;
    const double scale   = (scale_x < scale_y) ? scale_x : scale_y;

    const int target_w    = (int)lround(vis_w * scale);
    const int target_h    = (int)lround(vis_h * scale);
    const int black_bar_x = (out_w - target_w) / 2;
    const int black_bar_y = (out_h - target_h) / 2;
    const int view_x = view ? view->x : 0;
    const int view_y = view ? view->y : 0;

    int w = popup->width > 0 ? popup->width :
        (popup->xdg_surface && popup->xdg_surface->surface ?
            popup->xdg_surface->surface->current.width :
            0);
    int h = popup->height > 0 ? popup->height :
        (popup->xdg_surface && popup->xdg_surface->surface ?
            popup->xdg_surface->surface->current.height :
            0);

    if ((w > 0) && (h > 0))
    {
        struct wlr_box box = {
            .x     = (int)lround(view_x + black_bar_x + popup->x * scale),
            .y     = (int)lround(view_y + black_bar_y + popup->y * scale),
            .width = (int)lround(w * scale),
            .height = (int)lround(h * scale),
        };
        sparrow_damage_add_box(&box, true);
    } else
    {
        sparrow_damage_add_box(nullptr, true);
    }
}

static void popup_handle_map(struct wl_listener *listener, void *data)
{
    SparrowPopup *popup = wl_container_of(listener, popup, map);
    Core *instance = Core::instance();

    wlr_log(WLR_INFO, "Popup surface mapped: handle=%d", popup->handle);

    // Register external texture for this popup
    if (!popup->texture_registered)
    {
        popup->texture_id =
            popup->handle +
            200000; // Offset to avoid collision with views/subsurfaces
        FlutterEngineResult result = instance->embedder_api.RegisterExternalTexture(
            instance->engine, popup->texture_id);
        if (result == kSuccess)
        {
            popup->texture_registered = true;
            wlr_log(WLR_INFO, "Registered popup texture: %ld", popup->texture_id);
        } else
        {
            wlr_log(WLR_ERROR, "Failed to register popup texture");
        }
    }

    // Unconditionally focusing popups here caused browsers (Firefox/Chrome) to drop focus and immediately
    // unmap popups.
    // Seat state and activation are maintained without stealing keyboard focus on map.

    sparrow_popup_damage_whole(popup);

    // instance->callable_queue.enqueue([=] {
    send_popup_map(popup);
    // });
}

static void popup_handle_unmap(struct wl_listener *listener, void *data)
{
    SparrowPopup *popup = wl_container_of(listener, popup, unmap);
    Core *instance  = Core::instance();
    uint32_t handle = popup->handle;
    SparrowView *parent_view = popup->parent_view;

    wlr_log(WLR_INFO, "Popup surface unmapped: handle=%d", handle);

    // Unregister external texture if registered
    if (popup->texture_registered)
    {
        instance->embedder_api.UnregisterExternalTexture(instance->engine,
            popup->texture_id);
        popup->texture_registered = false;
        wlr_log(WLR_INFO, "Unregistered popup texture: %ld", popup->texture_id);
    }

    sparrow_popup_damage_whole(popup);

    // instance->callable_queue.enqueue([=] {
    send_popup_unmap(handle);
    // });

    if (parent_view)
    {
        sparrow_view_focus(parent_view);
    }
}

static void popup_handle_scene_tree_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowPopup *popup = wl_container_of(listener, popup, scene_tree_destroy);
    popup->scene_tree = nullptr;
    wl_list_remove(&popup->scene_tree_destroy.link);
    wl_list_init(&popup->scene_tree_destroy.link);
}

static void popup_handle_destroy(struct wl_listener *listener, void *data)
{
    SparrowPopup *popup = wl_container_of(listener, popup, destroy);
    Core *instance = Core::instance();
    SparrowView *parent_view = popup->parent_view;

    wlr_log(WLR_INFO, "Popup destroyed: handle=%d", popup->handle);

    pthread_mutex_lock(&instance->sparrow_renderer.texture_mutex);

    // Destroy scene tree safely (if not already destroyed by parent view destruction)
    if (popup->scene_tree != nullptr)
    {
        wl_list_remove(&popup->scene_tree_destroy.link);
        wl_list_init(&popup->scene_tree_destroy.link);
        wlr_scene_node_destroy(&popup->scene_tree->node);
        popup->scene_tree = nullptr;
    }

    if (popup->locked_buffer != nullptr)
    {
        wlr_buffer_unlock(popup->locked_buffer);
        popup->locked_buffer = nullptr;
    }

    // Clean up Flutter texture resources
    if (popup->texture_registered)
    {
        instance->embedder_api.UnregisterExternalTexture(instance->engine,
            popup->texture_id);
        popup->texture_registered = false;
    }

    // Clear button state tracking for this popup (uses +200000 offset)
    sparrow_clear_surface_buttons(popup->handle + 200000);

    // Remove listeners
    wl_list_remove(&popup->map.link);
    wl_list_remove(&popup->unmap.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->reposition.link);

    // Remove from handle map
    handle_map_remove(instance->popups, popup->handle);

    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);

    if (parent_view)
    {
        sparrow_view_focus(parent_view);
    }

    delete popup;
}

static void popup_handle_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowPopup *popup = wl_container_of(listener, popup, commit);
    Core *instance = Core::instance();

    // Unconstrain on first commit (when surface is initialized)
    // Use parent's output bounds for popup constraint (multi-monitor fix)
    if (!popup->unconstrained)
    {
        struct wlr_box output_box = {};

        // Use parent view's output for constraint
        Output *output = popup->parent_view ? popup->parent_view->current_output : nullptr;
        if ((output == nullptr) && popup->parent_view)
        {
            output = sparrow_output_for_box(popup->parent_view->x, popup->parent_view->y,
                popup->parent_view->width, popup->parent_view->height);
            if (output != nullptr)
            {
                popup->parent_view->current_output = output;
                popup->parent_view->output_scale   = output->wlr_output->scale;
            }
        }

        if (output == nullptr)
        {
            output = sparrow_get_first_output();
        }

        if ((output != nullptr) && (output->wlr_output != nullptr))
        {
            wlr_output_layout_get_box(instance->output_layout, output->wlr_output,
                &output_box);
            popup->current_output = output;
            popup->output_scale   = output->wlr_output->scale;
        } else
        {
            wlr_output_layout_get_box(instance->output_layout, nullptr, &output_box);
        }

        if ((output_box.width <= 0) || (output_box.height <= 0))
        {
            if (popup->parent_view && (popup->parent_view->width > 0) && (popup->parent_view->height > 0))
            {
                output_box.width  = popup->parent_view->width;
                output_box.height = popup->parent_view->height;
            }
        }

        // wlr_xdg_popup_unconstrain_from_box takes toplevel_space_box (coordinates relative to the root
        // toplevel view)
        const int parent_view_x = popup->parent_view ? popup->parent_view->x : 0;
        const int parent_view_y = popup->parent_view ? popup->parent_view->y : 0;
        struct wlr_box toplevel_space_box = {
            .x     = output_box.x - parent_view_x,
            .y     = output_box.y - parent_view_y,
            .width = output_box.width,
            .height = output_box.height,
        };

        wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &toplevel_space_box);
        popup->unconstrained = true;
        wlr_log(WLR_INFO,
            "Popup %d unconstrained: toplevel_space_box=(%d,%d,%dx%d)",
            popup->handle, toplevel_space_box.x, toplevel_space_box.y,
            toplevel_space_box.width, toplevel_space_box.height);
    }

    if (!popup->xdg_surface || !popup->xdg_surface->surface->mapped)
    {
        return;
    }

    pthread_mutex_lock(&instance->sparrow_renderer.texture_mutex);
    struct wlr_buffer *new_buf = nullptr;
    if (popup->xdg_surface->surface && popup->xdg_surface->surface->buffer)
    {
        new_buf = popup->xdg_surface->surface->buffer->source;
    }

    if (new_buf != popup->locked_buffer)
    {
        if (new_buf != nullptr)
        {
            wlr_buffer_lock(new_buf);
        }

        if (popup->locked_buffer != nullptr)
        {
            wlr_buffer_unlock(popup->locked_buffer);
        }

        popup->locked_buffer = new_buf;
    }

    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);

    // Fallback: if surface is mapped but texture isn't registered, do it now
    // This handles cases where the map event was missed
    if (!popup->texture_registered)
    {
        wlr_log(WLR_INFO, "Popup commit: surface mapped but texture not "
                          "registered, registering now");
        popup->texture_id = popup->handle + 200000;
        FlutterEngineResult result = instance->embedder_api.RegisterExternalTexture(
            instance->engine, popup->texture_id);
        if (result == kSuccess)
        {
            popup->texture_registered = true;
            wlr_log(WLR_INFO, "Registered popup texture via commit fallback: %ld",
                popup->texture_id);
            // instance->callable_queue.enqueue([=] {
            send_popup_map(popup);
            // });
        } else
        {
            wlr_log(WLR_ERROR,
                "Failed to register popup texture via commit fallback");
        }
    }

    // Check if content dimensions or position changed - this can happen after unconstrain,
    // reposition, or when the client resizes the popup content (e.g. Chrome omnibox suggestions).
    if (popup->texture_registered)
    {
        struct wlr_surface *surf = popup->xdg_surface->surface;
        struct wlr_surface *content_surface = surf;

        // Check for subsurfaces (Firefox uses these for popup content)
        struct wlr_subsurface *first_subsurface = nullptr;
        struct wlr_subsurface *subsurface;
        wl_list_for_each(subsurface, &surf->current.subsurfaces_below,
            current.link)
        {
            if ((subsurface->surface != nullptr) && subsurface->surface->mapped)
            {
                first_subsurface = subsurface;
                break;
            }
        }
        if (first_subsurface == nullptr)
        {
            wl_list_for_each(subsurface, &surf->current.subsurfaces_above,
                current.link)
            {
                if ((subsurface->surface != nullptr) && subsurface->surface->mapped)
                {
                    first_subsurface = subsurface;
                    break;
                }
            }
        }

        if (first_subsurface != nullptr)
        {
            content_surface = first_subsurface->surface;
        }

        struct wlr_box pos_geo = popup->xdg_popup->current.geometry;
        if ((pos_geo.width == 0) || (pos_geo.height == 0))
        {
            pos_geo = popup->xdg_popup->scheduled.geometry;
        }

        struct wlr_box win_geo = popup->xdg_surface->current.geometry;
        if ((win_geo.width == 0) || (win_geo.height == 0))
        {
            win_geo = popup->xdg_surface->pending.geometry;
        }

        const int new_pos_x =
            pos_geo.x + (popup->parent_popup ? popup->parent_popup->pos_x : 0);
        const int new_pos_y =
            pos_geo.y + (popup->parent_popup ? popup->parent_popup->pos_y : 0);
        const int new_x     = new_pos_x - win_geo.x;
        const int new_y     = new_pos_y - win_geo.y;
        const int new_width = content_surface->current.width > 0 ?
            content_surface->current.width :
            pos_geo.width;
        const int new_height = content_surface->current.height > 0 ?
            content_surface->current.height :
            pos_geo.height;

        // If dimensions or position changed, notify Flutter with updated geometry
        if ((new_width > 0) && (new_height > 0) &&
            ((new_width != popup->width) || (new_height != popup->height) ||
             (new_x != popup->x) || (new_y != popup->y)))
        {
            wlr_log(WLR_INFO,
                "Popup %d geometry/content changed: pos=(%d,%d)->(%d,%d), "
                "size=%dx%d->%dx%d, re-sending popup_map",
                popup->handle, popup->x, popup->y, new_x, new_y, popup->width,
                popup->height, new_width, new_height);
            send_popup_map(popup); // This will update popup->x/y/width/height and send to Flutter
        }
    }

    // Mark texture as needing update
    if (popup->texture_registered)
    {
        instance->embedder_api.MarkExternalTextureFrameAvailable(instance->engine,
            popup->texture_id);
    }

    if (instance->show_fps)
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_us = (uint64_t)ts.tv_sec * 1000000ULL + (ts.tv_nsec / 1000);
        instance->record_client_commit(now_us);
    }

    sparrow_popup_damage_whole(popup);
}

static void popup_handle_reposition(struct wl_listener *listener, void *data)
{
    SparrowPopup *popup = wl_container_of(listener, popup, reposition);

    // Update local position tracking from new geometry
    struct wlr_box pos_geo = popup->xdg_popup->current.geometry;
    if ((pos_geo.width == 0) || (pos_geo.height == 0))
    {
        pos_geo = popup->xdg_popup->scheduled.geometry;
    }

    struct wlr_box win_geo = popup->xdg_surface->current.geometry;
    if ((win_geo.width == 0) || (win_geo.height == 0))
    {
        win_geo = popup->xdg_surface->pending.geometry;
    }

    popup->pos_x =
        pos_geo.x + (popup->parent_popup ? popup->parent_popup->pos_x : 0);
    popup->pos_y =
        pos_geo.y + (popup->parent_popup ? popup->parent_popup->pos_y : 0);

    popup->x     = popup->pos_x - win_geo.x;
    popup->y     = popup->pos_y - win_geo.y;
    popup->width = pos_geo.width;
    popup->height = pos_geo.height;

    wlr_log(WLR_INFO, "Popup repositioned: handle=%d, pos=(%d,%d), size=%dx%d",
        popup->handle, popup->x, popup->y, popup->width, popup->height);

    // Send updated position to Flutter (for any overlay UI)
    if (popup->xdg_surface->surface->mapped)
    {
        // instance->callable_queue.enqueue([=] {
        send_popup_map(popup); // Re-send with new position
        // });
    }
}

void sparrow_new_xdg_popup(struct wl_listener *listener, void *data)
{
    (void)listener;
    Core *instance = Core::instance();
    struct wlr_xdg_popup *xdg_popup = static_cast<struct wlr_xdg_popup*>(data);

    // Find the parent view
    struct wlr_xdg_surface *parent_xdg =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (parent_xdg == nullptr)
    {
        wlr_log(WLR_ERROR, "Popup parent is not an xdg_surface");
        return;
    }

    SparrowView *parent_view = instance->find_view_by_xdg_surface(parent_xdg);

    SparrowPopup *parent_popup = nullptr;
    if (parent_view == nullptr)
    {
        wlr_log(
            WLR_INFO,
            "Could not find parent view for popup, trying to find parent popup");
        if (instance->popups)
        {
            for (const auto &[handle, value] : *instance->popups)
            {
                auto *popup = static_cast<SparrowPopup*>(value);
                if ((popup != nullptr) && (popup->xdg_surface == parent_xdg))
                {
                    parent_popup = popup;
                    break;
                }
            }
        }

        if (parent_popup == nullptr)
        {
            wlr_log(WLR_ERROR, "Could not find any parent for popup");

            return;
        }
    }

    SparrowPopup *popup = new SparrowPopup();
    if (!popup)
    {
        wlr_log(WLR_ERROR, "Failed to allocate popup");
        return;
    }

    popup->xdg_popup   = xdg_popup;
    popup->xdg_surface = xdg_popup->base;
    popup->parent_view =
        parent_view ? parent_view : parent_popup->first_popup->parent_view;
    popup->parent_popup = parent_popup;
    popup->first_popup  = parent_view ? popup : parent_popup->first_popup;

    // Get initial geometry
    struct wlr_box pos_geo = xdg_popup->scheduled.geometry;
    if ((pos_geo.width == 0) || (pos_geo.height == 0))
    {
        pos_geo = xdg_popup->current.geometry;
    }

    struct wlr_box win_geo = xdg_popup->base->pending.geometry;
    if ((win_geo.width == 0) || (win_geo.height == 0))
    {
        win_geo = xdg_popup->base->current.geometry;
    }

    popup->pos_x =
        pos_geo.x + (parent_popup ? parent_popup->pos_x : 0);
    popup->pos_y =
        pos_geo.y + (parent_popup ? parent_popup->pos_y : 0);
    popup->x     = popup->pos_x - win_geo.x;
    popup->y     = popup->pos_y - win_geo.y;
    popup->width = pos_geo.width;
    popup->height = pos_geo.height;

    // Inherit output from parent view for multi-monitor support
    popup->current_output = popup->parent_view->current_output;
    popup->output_scale   = popup->parent_view->output_scale;

    popup->handle = handle_map_add(instance->popups, static_cast<void*>(popup));

    wlr_log(WLR_INFO,
        "Created popup: handle=%d, parent=%d, geo=(%d,%d,%dx%d), output=%s, "
        "scale=%.2f",
        popup->handle, popup->parent_view->handle, popup->x, popup->y,
        popup->width, popup->height,
        popup->current_output ? popup->current_output->wlr_output->name :
        "none",
        popup->output_scale);

    // Create popup scene tree as child of parent's scene tree (like tinywl)
    // This way popup positioning is automatic - relative to parent
    wl_list_init(&popup->scene_tree_destroy.link);
    if (popup->parent_view->scene_xdg_tree != nullptr)
    {
        popup->scene_tree = wlr_scene_xdg_surface_create(
            popup->parent_view->scene_xdg_tree, xdg_popup->base);
        if (popup->scene_tree != nullptr)
        {
            popup->scene_tree->node.data     = popup;
            popup->scene_tree_destroy.notify = popup_handle_scene_tree_destroy;
            wl_signal_add(&popup->scene_tree->node.events.destroy,
                &popup->scene_tree_destroy);
            wlr_log(WLR_INFO, "Created popup scene as child of parent view %d",
                popup->parent_view->handle);
        }
    } else
    {
        wlr_log(WLR_ERROR, "Parent view %d has no scene tree for popup",
            popup->parent_view->handle);
    }

    // Store popup in xdg_surface data for hit testing
    xdg_popup->base->data = popup;

    // Setup listeners
    popup->map.notify = popup_handle_map;
    wl_signal_add(&xdg_popup->base->surface->events.map, &popup->map);

    popup->unmap.notify = popup_handle_unmap;
    wl_signal_add(&xdg_popup->base->surface->events.unmap, &popup->unmap);

    popup->destroy.notify = popup_handle_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);

    popup->commit.notify = popup_handle_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    popup->reposition.notify = popup_handle_reposition;
    wl_signal_add(&xdg_popup->events.reposition, &popup->reposition);

    // Check if surface is already mapped (can happen in some cases)
    wlr_log(WLR_INFO, "Popup surface mapped state: %s",
        xdg_popup->base->surface->mapped ? "true" : "false");
    if (xdg_popup->base->surface->mapped)
    {
        wlr_log(WLR_INFO, "Popup surface already mapped, triggering map handler");
        popup_handle_map(&popup->map, nullptr);
    }
}

void sparrow_focus_popup(SparrowPopup *popup)
{
    if ((popup == nullptr) || (popup->xdg_surface == nullptr) ||
        (popup->xdg_surface->surface == nullptr))
    {
        return;
    }

    // Always keep parent toplevel activated while popup is active
    if (popup->parent_view && popup->parent_view->toplevel)
    {
        wlr_xdg_toplevel_set_activated(popup->parent_view->toplevel, true);
        popup->parent_view->activated = true;
    }
}
