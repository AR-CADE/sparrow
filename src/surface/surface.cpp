#include <cstdint>

#include <EGL/egl.h>

#include <sparrow/nonstd/wlroots-full.hpp>

#include "core.hpp"
#include "flutter/platform/cursor.hpp"
#include "flutter/platform/engine/messages/sub_surface_message.hpp"
#include "flutter/platform/engine/messages/surface_message.hpp"
#include "output.hpp"

#include "input/pointer.hpp"
#include "input/seat.hpp"
#include "popup.hpp"
#include "sub_surface.hpp"
#include "surface.hpp"
#include "view.hpp"

static uint32_t next_handle()
{
    static uint32_t texture_id = 1;
    return texture_id++;
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, map);
    Core *instance    = Core::instance();

    wlr_log(WLR_INFO, "XDG TOPLEVEL MAP: view=%d", view->handle);

    // Create scene graph for input hit-testing (rendering is still via Flutter
    // textures)
    sparrow_view_create_scene(view);
    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_set_enabled(&view->scene_tree->node, true);
    }

    struct wlr_box geo = view->xdg_surface->current.geometry;
    if ((geo.width <= 0) || (geo.height <= 0))
    {
        geo = view->xdg_surface->pending.geometry;
    }

    if ((geo.width <= 0) && view->xdg_surface->surface)
    {
        geo.width = view->xdg_surface->surface->current.width;
    }

    if ((geo.height <= 0) && view->xdg_surface->surface)
    {
        geo.height = view->xdg_surface->surface->current.height;
    }

    view->width  = geo.width;
    view->height = geo.height;
    view->geo_x  = geo.x;
    view->geo_y  = geo.y;

    wlr_log(WLR_INFO, "Geometry: buffer offset=(%d,%d), visible size=%dx%d",
        view->geo_x, view->geo_y, view->width, view->height);

    // Initialize output tracking for multi-monitor support
    view->current_output =
        sparrow_output_for_box(view->x, view->y, view->width, view->height);
    view->output_scale =
        view->current_output ? view->current_output->wlr_output->scale : 1.0;

    wlr_log(WLR_INFO, "View %d initial output: %s (scale=%.2f)", view->handle,
        view->current_output ? view->current_output->wlr_output->name :
        "none",
        view->output_scale);

    pthread_mutex_lock(&instance->sparrow_renderer.texture_mutex);
    view->texture_id = (int64_t)view->handle;
    FlutterEngineResult result = instance->embedder_api.RegisterExternalTexture(
        instance->engine, view->texture_id);
    if (result == kSuccess)
    {
        view->texture_registered = true;
        wlr_log(WLR_INFO, "Registered external texture %ld for view %d",
            view->texture_id, view->handle);
    } else
    {
        wlr_log(WLR_ERROR, "Failed to register external texture for view %d",
            view->handle);
        view->texture_registered = false;
    }

    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);

    sparrow_view_focus(view);

    sparrow_view_damage_whole(view);

    // instance->callable_queue.enqueue([=] {
    send_surface_map(view);
    // });
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, unmap);
    Core *instance    = Core::instance();
    uint32_t handle   = view->handle;

    if (instance->seat && view->xdg_surface &&
        (instance->seat->pointer_state.focused_surface ==
         view->xdg_surface->surface))
    {
        sparrow_pointer_constraints_deactivate(instance);
        wlr_seat_pointer_clear_focus(instance->seat);
        sparrow_cursor_reset_to_flutter();
    }

    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_set_enabled(&view->scene_tree->node, false);
    }

    // Destroy foreign toplevel handles on unmap
    if (view->foreign_toplevel != nullptr)
    {
        wl_list_remove(&view->foreign_activate_request.link);
        wl_list_remove(&view->foreign_close_request.link);
        wl_list_remove(&view->foreign_maximize_request.link);
        wl_list_remove(&view->foreign_minimize_request.link);
        wl_list_remove(&view->foreign_fullscreen_request.link);
        wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
        view->foreign_toplevel = nullptr;
    }

    if (view->ext_foreign_toplevel != nullptr)
    {
        wlr_ext_foreign_toplevel_handle_v1_destroy(view->ext_foreign_toplevel);
        view->ext_foreign_toplevel = nullptr;
    }

    sparrow_view_damage_whole(view);

    // instance->callable_queue.enqueue([=] {
    send_surface_unmap(handle);
    // });
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, set_title);
    if (!view->toplevel->base->initialized ||
        !view->toplevel->base->initial_commit)
    {
        return;
    }

    wlr_log(WLR_INFO, "set_title: %s", view->toplevel->title);

    if (view->foreign_toplevel != nullptr)
    {
        wlr_foreign_toplevel_handle_v1_set_title(
            view->foreign_toplevel,
            view->toplevel->title ? view->toplevel->title : "");
    }

    if (view->ext_foreign_toplevel != nullptr)
    {
        struct wlr_ext_foreign_toplevel_handle_v1_state ext_state = {
            .title  = view->toplevel->title ? view->toplevel->title : "",
            .app_id = view->toplevel->app_id ? view->toplevel->app_id : "",
        };
        wlr_ext_foreign_toplevel_handle_v1_update_state(view->ext_foreign_toplevel,
            &ext_state);
    }

    // view->instance->callable_queue.enqueue([=] {
    send_surface_title(view);
    // });
}

static void xdg_toplevel_set_app_id(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, set_app_id);
    if (!view->toplevel->base->initialized ||
        !view->toplevel->base->initial_commit)
    {
        return;
    }

    wlr_log(WLR_INFO, "set_app_id: %s", view->toplevel->app_id);

    if (view->foreign_toplevel != nullptr)
    {
        wlr_foreign_toplevel_handle_v1_set_app_id(
            view->foreign_toplevel,
            view->toplevel->app_id ? view->toplevel->app_id : "");
    }

    if (view->ext_foreign_toplevel != nullptr)
    {
        struct wlr_ext_foreign_toplevel_handle_v1_state ext_state = {
            .title  = view->toplevel->title ? view->toplevel->title : "",
            .app_id = view->toplevel->app_id ? view->toplevel->app_id : "",
        };
        wlr_ext_foreign_toplevel_handle_v1_update_state(view->ext_foreign_toplevel,
            &ext_state);
    }

    // view->instance->callable_queue.enqueue([=] {
    send_surface_title(view);
    // });
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, commit);

    struct wlr_box geo = view->xdg_surface->current.geometry;
    if ((geo.width <= 0) || (geo.height <= 0))
    {
        geo = view->xdg_surface->pending.geometry;
    }

    if ((geo.width <= 0) && view->xdg_surface->surface)
    {
        geo.width = view->xdg_surface->surface->current.width;
    }

    if ((geo.height <= 0) && view->xdg_surface->surface)
    {
        geo.height = view->xdg_surface->surface->current.height;
    }

    // Check if geometry changed
    const bool geo_changed =
        (view->width != geo.width || view->height != geo.height ||
            view->geo_x != geo.x || view->geo_y != geo.y);

    view->width  = geo.width;
    view->height = geo.height;
    view->geo_x  = geo.x;
    view->geo_y  = geo.y;

    sparrow_view_update_scene(view);

    // Notify Dart of geometry changes so it can update clipping/sizing
    if (geo_changed)
    {
        send_surface_geometry(view);
        if (view->activated)
        {
            sparrow_view_damage_whole(view);
        }
    }

    // Check if any child subsurface changed position/dimensions on parent commit
    SparrowSubSurface *sub;
    wl_list_for_each(sub, &view->subsurfaces, link)
    {
        const int old_x  = sub->x;
        const int old_y  = sub->y;
        const int old_w  = sub->width;
        const int old_h  = sub->height;
        const int old_bw = sub->buffer_width;
        const int old_bh = sub->buffer_height;
        sub->width  = sub->surface->current.width;
        sub->height = sub->surface->current.height;
        sub->buffer_width = sub->surface->current.buffer_width > 0 ?
            sub->surface->current.buffer_width :
            sub->width;
        sub->buffer_height = sub->surface->current.buffer_height > 0 ?
            sub->surface->current.buffer_height :
            sub->height;

        int parent_root_x = 0;
        int parent_root_y = 0;
        struct wlr_subsurface *curr_sub = sub->wlr_subsurface;
        while (curr_sub != nullptr)
        {
            parent_root_x += curr_sub->current.x;
            parent_root_y += curr_sub->current.y;
            if ((curr_sub->parent == nullptr) ||
                (sub->parent_view && sub->parent_view->xdg_surface &&
                 (curr_sub->parent == sub->parent_view->xdg_surface->surface)))
            {
                break;
            }

            curr_sub = wlr_subsurface_try_from_wlr_surface(curr_sub->parent);
        }

        sub->x = parent_root_x;
        sub->y = parent_root_y;

        if ((old_x != sub->x) || (old_y != sub->y) || (old_w != sub->width) ||
            (old_h != sub->height) || (old_bw != sub->buffer_width) ||
            (old_bh != sub->buffer_height))
        {
            sparrow_subsurface_send_position(sub);
        }
    }
    Core *instance = Core::instance();

    // Track client commit interval and buffer drop anomalies
    struct timespec ts_commit;
    clock_gettime(CLOCK_MONOTONIC, &ts_commit);
    uint64_t commit_now_us =
        (uint64_t)ts_commit.tv_sec * 1000000ULL + (ts_commit.tv_nsec / 1000);

    view->commit_count++;
    double commit_dt_ms =
        (view->last_commit_time_us > 0) ?
        (double)(commit_now_us - view->last_commit_time_us) / 1000.0 :
        0.0;
    view->last_commit_time_us = commit_now_us;

    // Safely update locked buffer reference for multi-threaded Flutter rasterizer
    pthread_mutex_lock(&instance->sparrow_renderer.texture_mutex);
    struct wlr_buffer *new_buf = nullptr;
    if (view->xdg_surface && view->xdg_surface->surface &&
        view->xdg_surface->surface->buffer)
    {
        new_buf = view->xdg_surface->surface->buffer->source;
    }

    if (new_buf != view->locked_buffer)
    {
        if (!view->current_buffer_sampled && (view->commit_count > 1))
        {
            view->dropped_buffer_count++;
            if (instance->debug_pacing)
            {
                const char *app = (view->toplevel && view->toplevel->app_id) ?
                    view->toplevel->app_id :
                    "unknown";
                wlr_log(WLR_INFO,
                    "[PACING-DROP] Client '%s' commit #%lu arrived before previous "
                    "buffer was sampled! dt=%.2fms (dropped: %lu/%lu)",
                    app, (unsigned long)view->commit_count, commit_dt_ms,
                    (unsigned long)view->dropped_buffer_count,
                    (unsigned long)view->commit_count);
            }
        }

        if (new_buf != nullptr)
        {
            wlr_buffer_lock(new_buf);
        }

        if (view->locked_buffer != nullptr)
        {
            wlr_buffer_unlock(view->locked_buffer);
        }

        view->locked_buffer = new_buf;
        view->current_buffer_sampled = false;
    }

    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);

    // Trigger Flutter texture updates and output damage for any mapped view
    if (view->xdg_surface && view->xdg_surface->surface &&
        view->xdg_surface->surface->mapped)
    {
        if (view->texture_registered)
        {
            instance->embedder_api.MarkExternalTextureFrameAvailable(
                instance->engine, view->texture_id);
        }

        pixman_region32_t damage;
        pixman_region32_init(&damage);
        wlr_surface_get_effective_damage(view->xdg_surface->surface, &damage);

        if (pixman_region32_not_empty(&damage))
        {
            if (instance->show_fps)
            {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                uint64_t now_us =
                    (uint64_t)ts.tv_sec * 1000000ULL + (ts.tv_nsec / 1000);
                instance->record_client_commit(now_us);
            }

            int nrects = 0;
            pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
            if (instance->debug_protocol)
            {
                wlr_log(WLR_DEBUG, "XDG COMMIT view %d: damage nrects=%d (%d,%d %dx%d)",
                    view->handle, nrects, rects[0].x1, rects[0].y1,
                    rects[0].x2 - rects[0].x1, rects[0].y2 - rects[0].y1);
            }

            for (int i = 0; i < nrects; ++i)
            {
                sparrow_view_damage_add_rect(view, rects[i].x1, rects[i].y1,
                    rects[i].x2 - rects[i].x1,
                    rects[i].y2 - rects[i].y1);
            }
        }

        pixman_region32_fini(&damage);
    }

    if (!view->toplevel->base->initialized ||
        !view->toplevel->base->initial_commit)
    {
        return;
    }

    wlr_xdg_toplevel_set_wm_capabilities(
        view->toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU |
        WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE |
        WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN |
        WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE);

    // Send initial configure event so client can commit and map
    Output *out = sparrow_get_first_output();
    int eff_w   = 0;
    int eff_h   = 0;
    if (out && out->wlr_output)
    {
        wlr_output_effective_resolution(out->wlr_output, &eff_w, &eff_h);
    }

    view->maximized = true;
    wlr_xdg_toplevel_set_size(view->toplevel, eff_w, eff_h);
    wlr_xdg_toplevel_set_maximized(view->toplevel, true);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, destroy);
    Core *instance    = Core::instance();

    pthread_mutex_lock(&instance->sparrow_renderer.texture_mutex);

    if (view->locked_buffer != nullptr)
    {
        wlr_buffer_unlock(view->locked_buffer);
        view->locked_buffer = nullptr;
    }

    if (view->texture_registered)
    {
        instance->embedder_api.UnregisterExternalTexture(instance->engine,
            view->texture_id);
        view->texture_registered = false;
    }

    // Clear button state tracking for this surface
    sparrow_clear_surface_buttons(view->handle);

    sparrow_view_destroy_scene(view);

    // Remove decoration listeners if attached to avoid Use-After-Free
    if (view->decoration != nullptr)
    {
        if (view->decoration_request_mode.link.next &&
            view->decoration_request_mode.link.prev)
        {
            wl_list_remove(&view->decoration_request_mode.link);
            wl_list_init(&view->decoration_request_mode.link);
        }

        if (view->decoration_destroy.link.next &&
            view->decoration_destroy.link.prev)
        {
            wl_list_remove(&view->decoration_destroy.link);
            wl_list_init(&view->decoration_destroy.link);
        }

        view->decoration = nullptr;
    }

    if (view->foreign_toplevel != nullptr)
    {
        wl_list_remove(&view->foreign_activate_request.link);
        wl_list_remove(&view->foreign_close_request.link);
        wl_list_remove(&view->foreign_maximize_request.link);
        wl_list_remove(&view->foreign_minimize_request.link);
        wl_list_remove(&view->foreign_fullscreen_request.link);
        wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
        view->foreign_toplevel = nullptr;
    }

    if (view->ext_foreign_toplevel != nullptr)
    {
        wlr_ext_foreign_toplevel_handle_v1_destroy(view->ext_foreign_toplevel);
        view->ext_foreign_toplevel = nullptr;
    }

    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->set_title.link);
    wl_list_remove(&view->set_app_id.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->new_subsurface.link);
    wl_list_remove(&view->link);

    // Detach child subsurfaces from this view to prevent Use-After-Free
    SparrowSubSurface *sub, *sub_tmp;
    wl_list_for_each_safe(sub, sub_tmp, &view->subsurfaces, link)
    {
        sub->parent_view = nullptr;
        wl_list_remove(&sub->link);
        wl_list_init(&sub->link);
    }

    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);

    delete (view);
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
    void *data)
{
    SparrowView *view = wl_container_of(listener, view, request_move);
    Core *instance    = Core::instance();

    wlr_log(WLR_INFO, "CSD app requested move for view %d at cursor (%.1f, %.1f)",
        view->handle, instance->input.flutter_cursor_x,
        instance->input.flutter_cursor_y);

    sparrow_view_focus(view);
    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
    void *data)
{
    SparrowView *view = wl_container_of(listener, view, request_resize);
    struct wlr_xdg_toplevel_resize_event *event =
        static_cast<wlr_xdg_toplevel_resize_event*>(data);
    Core *instance = Core::instance();

    wlr_log(
        WLR_INFO,
        "CSD app requested resize for view %d, edges=%d at cursor (%.1f, %.1f)",
        view->handle, event->edges, instance->input.flutter_cursor_x,
        instance->input.flutter_cursor_y);

    sparrow_view_focus(view);

    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }
}

void sparrow_handle_xdg_activation_request_activate(
    struct wl_listener *listener, void *data)
{
    Core *instance = Core::instance();
    struct wlr_xdg_activation_v1_request_activate_event *event =
        static_cast<struct wlr_xdg_activation_v1_request_activate_event*>(data);

    if (!event || !event->surface)
    {
        return;
    }

    struct wlr_surface *surface = event->surface;
    SparrowView *view = instance->find_view_by_wlr_surface(surface);
    if (!view)
    {
        struct wlr_surface *root = wlr_surface_get_root_surface(surface);
        if (root)
        {
            view = instance->find_view_by_wlr_surface(root);
        }
    }

    if (view && view->toplevel->base->initialized)
    {
        const char *token_str =
            event->token ? wlr_xdg_activation_token_v1_get_name(event->token) : "";
        if (!token_str)
        {
            token_str = "";
        }

        const char *app_id =
            (event->token && event->token->app_id) ?
            event->token->app_id :
            (view->toplevel && view->toplevel->app_id ? view->toplevel->app_id :
                "");

        wlr_log(WLR_INFO,
            "xdg-activation request_activate for view %d (app_id='%s', "
            "token='%s')",
            view->handle, app_id, token_str);

        sparrow_view_focus(view);
        send_surface_request_activate(view->handle, token_str, app_id);
    }
}

void sparrow_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    (void)listener;
    Core *instance = Core::instance();
    struct wlr_xdg_toplevel *toplevel = static_cast<wlr_xdg_toplevel*>(data);
    if (toplevel == nullptr)
    {
        return;
    }

    struct wlr_xdg_surface *xdg_surface = toplevel->base;

    if (xdg_surface == nullptr)
    {
        return;
    }

    SparrowView *view = new SparrowView();
    if (view == nullptr)
    {
        return;
    }

    wlr_log(WLR_INFO, "NEW XDG TOPLEVEL: %p, surface mapped=%d", (void*)toplevel,
        xdg_surface->surface->mapped);

    view->xdg_surface = xdg_surface;
    view->toplevel    = toplevel;

    wl_list_insert(&instance->views_list, &view->link);

    // Initialize subsurface list
    wl_list_init(&view->subsurfaces);

    view->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);
    view->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
    view->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&toplevel->events.destroy, &view->destroy);
    view->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);
    view->set_title.notify = xdg_toplevel_set_title;
    wl_signal_add(&toplevel->events.set_title, &view->set_title);
    view->set_app_id.notify = xdg_toplevel_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &view->set_app_id);

    // Handle CSD app move/resize requests (when user drags their own titlebar)
    view->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&toplevel->events.request_move, &view->request_move);
    view->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &view->request_resize);

    // Listen for new subsurfaces
    view->new_subsurface.notify = sparrow_subsurface_handle_new;
    wl_signal_add(&xdg_surface->surface->events.new_subsurface,
        &view->new_subsurface);

    view->handle = next_handle();

    // const int offset = (int)(view_handle % 10) * 32;
    view->x     = 0;
    view->y     = 0;
    view->width = 0;
    view->height     = 0;
    view->maximized  = false;
    view->fullscreen = false;
    view->activated  = false;

    // Decoration tracking - will be set by sparrow_handle_new_toplevel_decoration
    // if client supports xdg-decoration
    view->decoration = nullptr;
    view->uses_ssd   = false;
    wl_list_init(&view->decoration_request_mode.link);
    wl_list_init(&view->decoration_destroy.link);

    xdg_surface->data = view;

    // Scan any subsurfaces that might already exist on this surface
    struct wlr_subsurface *sub;
    wl_list_for_each(sub, &xdg_surface->surface->current.subsurfaces_below,
        current.link)
    {
        sparrow_subsurface_create(view, sub);
    }
    wl_list_for_each(sub, &xdg_surface->surface->current.subsurfaces_above,
        current.link)
    {
        sparrow_subsurface_create(view, sub);
    }
}
