#include "flutter_embedder.h"
#include "flutter/platform/engine/messages/sub_surface_message.hpp"

#include "core.hpp"
#include "view.hpp"
#include "sub_surface.hpp"



static void sparrow_sub_surface_get_root_coords(SparrowSubSurface *sub,
    int *root_x, int *root_y)
{
    int x = 0;
    int y = 0;
    struct wlr_subsurface *curr = sub ? sub->wlr_subsurface : nullptr;
    while (curr != nullptr)
    {
        x += curr->current.x;
        y += curr->current.y;
        if ((curr->parent == nullptr) ||
            (sub->parent_view && sub->parent_view->xdg_surface &&
             (curr->parent == sub->parent_view->xdg_surface->surface)))
        {
            break;
        }

        curr = wlr_subsurface_try_from_wlr_surface(curr->parent);
    }

    *root_x = x;
    *root_y = y;
}

static void sparrow_subsurface_handle_map(struct wl_listener *listener, void *data)
{
    SparrowSubSurface *sub = wl_container_of(listener, sub, map);
    Core *instance = Core::instance();

    wlr_log(WLR_INFO, "Subsurface MAP: handle=%d parent=%d", sub->handle,
        sub->parent_handle);

    // Get surface dimensions
    sub->width  = sub->surface->current.width;
    sub->height = sub->surface->current.height;
    sub->buffer_width = sub->surface->current.buffer_width > 0 ?
        sub->surface->current.buffer_width :
        sub->width;
    sub->buffer_height = sub->surface->current.buffer_height > 0 ?
        sub->surface->current.buffer_height :
        sub->height;

    // Get position relative to root toplevel parent
    sparrow_sub_surface_get_root_coords(sub, &sub->x, &sub->y);

    // Register external texture
    // Offset subsurface texture IDs by 100000 to avoid collision with view
    // texture IDs
    sub->texture_id = (int64_t)(100000 + sub->handle);
    FlutterEngineResult result = instance->embedder_api.RegisterExternalTexture(
        instance->engine, sub->texture_id);
    if (result == kSuccess)
    {
        sub->texture_registered = true;
        wlr_log(WLR_INFO, "Registered subsurface texture %ld (handle=%d)",
            sub->texture_id, sub->handle);
    } else
    {
        wlr_log(WLR_ERROR, "Failed to register subsurface texture");
        sub->texture_registered = false;
    }

    // Send to Dart
    // instance->callable_queue.enqueue([=] {
    send_subsurface_map(sub);
    // });
}

static void sparrow_subsurface_handle_unmap(struct wl_listener *listener, void *data)
{
    SparrowSubSurface *sub = wl_container_of(listener, sub, unmap);
    uint32_t handle = sub->handle;
    uint32_t parent_handle = sub->parent_handle;

    wlr_log(WLR_INFO, "Subsurface UNMAP: handle=%d", sub->handle);

    // Send to Dart
    send_subsurface_unmap(handle, parent_handle);
}

void sparrow_subsurface_damage_add_rect(SparrowSubSurface *sub, int x, int y,
    int width, int height)
{
    if (!sub || !sub->parent_view)
    {
        return;
    }

    if ((width <= 0) || (height <= 0))
    {
        return;
    }

    Core *instance = Core::instance();
    if (instance && instance->force_render_all_views)
    {
        sparrow_damage_add_box(nullptr, false);
        return;
    }

    SparrowView *view = sub->parent_view;
    Output *output    = view->current_output ? view->current_output : sparrow_get_first_output();
    if (!output || !output->wlr_output)
    {
        return;
    }

    const int out_w = output->wlr_output->width;
    const int out_h = output->wlr_output->height;

    const int vis_w = view->width > 0 ? view->width : out_w;
    const int vis_h = view->height > 0 ? view->height : out_h;
    const double scale_x = (vis_w > 0) ? ((double)out_w / (double)vis_w) : 1.0;
    const double scale_y = (vis_h > 0) ? ((double)out_h / (double)vis_h) : 1.0;
    const double scale   = (scale_x < scale_y) ? scale_x : scale_y;

    const int target_w    = (int)lround(vis_w * scale);
    const int target_h    = (int)lround(vis_h * scale);
    const int black_bar_x = (out_w - target_w) / 2;
    const int black_bar_y = (out_h - target_h) / 2;

    // In Dart, SubSurface widget is placed at `left: sub.x * scale, top: sub.y * scale` inside SurfaceTree
    // (which is centered with black bars)
    const int mapped_x = (int)lround(view->x + black_bar_x + (sub->x + x) * scale);
    const int mapped_y = (int)lround(view->y + black_bar_y + (sub->y + y) * scale);
    const int mapped_w = (int)lround(width * scale);
    const int mapped_h = (int)lround(height * scale);

    struct wlr_box box = {
        .x     = mapped_x,
        .y     = mapped_y,
        .width = mapped_w,
        .height = mapped_h,
    };
    sparrow_damage_add_box(&box, true);
}

static void sparrow_subsurface_handle_commit(struct wl_listener *listener, void *data)
{
    SparrowSubSurface *sub = wl_container_of(listener, sub, commit);
    Core *instance = Core::instance();

    // Update dimensions and position
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

    sparrow_sub_surface_get_root_coords(sub, &sub->x, &sub->y);

    // If position/size changed, notify Dart
    if ((old_x != sub->x) || (old_y != sub->y) || (old_w != sub->width) ||
        (old_h != sub->height) || (old_bw != sub->buffer_width) ||
        (old_bh != sub->buffer_height))
    {
        if (instance->debug_protocol)
        {
            wlr_log(WLR_DEBUG,
                "Subsurface %d sending position update: %dx%d (buf=%dx%d) @ (%d,%d)",
                sub->handle, sub->width, sub->height, sub->buffer_width,
                sub->buffer_height, sub->x, sub->y);
        }

        sparrow_subsurface_send_position(sub);
        if (sub->parent_view && sub->parent_view->activated)
        {
            if ((old_w > 0) && (old_h > 0))
            {
                sparrow_subsurface_damage_add_rect(sub, 0, 0, old_w, old_h);
            }

            sparrow_subsurface_damage_add_rect(sub, 0, 0, sub->width, sub->height);
        }
    }

    pthread_mutex_lock(&instance->sparrow_renderer.texture_mutex);
    struct wlr_buffer *new_buf = nullptr;
    if (sub->surface && sub->surface->buffer)
    {
        new_buf = sub->surface->buffer->source;
    }

    if (new_buf != sub->locked_buffer)
    {
        if (new_buf != nullptr)
        {
            wlr_buffer_lock(new_buf);
        }

        if (sub->locked_buffer != nullptr)
        {
            wlr_buffer_unlock(sub->locked_buffer);
        }

        sub->locked_buffer = new_buf;
    }

    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);

    // Notify Flutter and add damage for any mapped subsurface
    if (sub->surface && sub->surface->mapped)
    {
        // Notify Flutter that texture has new frame
        if (sub->texture_registered)
        {
            instance->embedder_api.MarkExternalTextureFrameAvailable(instance->engine,
                sub->texture_id);
        }

        pixman_region32_t damage;
        pixman_region32_init(&damage);
        wlr_surface_get_effective_damage(sub->surface, &damage);

        if (pixman_region32_not_empty(&damage))
        {
            if (instance->show_fps)
            {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                uint64_t now_us = (uint64_t)ts.tv_sec * 1000000ULL + (ts.tv_nsec / 1000);
                instance->record_client_commit(now_us);
            }

            int nrects = 0;
            pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
            for (int i = 0; i < nrects; ++i)
            {
                sparrow_subsurface_damage_add_rect(
                    sub, rects[i].x1, rects[i].y1,
                    rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);
            }
        }

        pixman_region32_fini(&damage);
    }
}

static void sparrow_subsurface_handle_destroy(struct wl_listener *listener,
    void *data)
{
    SparrowSubSurface *sub = wl_container_of(listener, sub, destroy);
    Core *instance = Core::instance();

    wlr_log(WLR_INFO, "Subsurface DESTROY: handle=%d", sub->handle);

    pthread_mutex_lock(&instance->sparrow_renderer.texture_mutex);

    if (sub->locked_buffer != nullptr)
    {
        wlr_buffer_unlock(sub->locked_buffer);
        sub->locked_buffer = nullptr;
    }

    // Unregister texture
    if (sub->texture_registered)
    {
        instance->embedder_api.UnregisterExternalTexture(instance->engine,
            sub->texture_id);
        sub->texture_registered = false;
    }

    // Remove from handle map
    handle_map_remove(instance->subsurfaces, sub->handle);

    // Remove from parent view list if still linked
    if ((sub->link.next != nullptr) && (sub->link.prev != nullptr))
    {
        wl_list_remove(&sub->link);
        wl_list_init(&sub->link);
    }

    // Remove listeners
    wl_list_remove(&sub->map.link);
    wl_list_remove(&sub->unmap.link);
    wl_list_remove(&sub->destroy.link);
    wl_list_remove(&sub->commit.link);
    if (sub->new_subsurface.link.next && sub->new_subsurface.link.prev)
    {
        wl_list_remove(&sub->new_subsurface.link);
        wl_list_init(&sub->new_subsurface.link);
    }

    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);

    delete sub;
}

static void sparrow_subsurface_handle_new_on_subsurface(struct wl_listener *listener,
    void *data)
{
    SparrowSubSurface *parent_sub =
        wl_container_of(listener, parent_sub, new_subsurface);
    struct wlr_subsurface *child_wlr_subsurface =
        static_cast<struct wlr_subsurface*>(data);
    if (parent_sub && parent_sub->parent_view)
    {
        wlr_log(WLR_INFO, "New nested subsurface for view %d (parent sub=%d)",
            parent_sub->parent_view->handle, parent_sub->handle);
        sparrow_subsurface_create(parent_sub->parent_view, child_wlr_subsurface);
    }
}

SparrowSubSurface *sparrow_subsurface_create(SparrowView *view, struct wlr_subsurface *wlr_subsurface)
{
    if ((view == nullptr) || (wlr_subsurface == nullptr) ||
        (wlr_subsurface->surface == nullptr))
    {
        return nullptr;
    }

    // Avoid duplicate creation if already registered
    SparrowSubSurface *existing_sub;
    wl_list_for_each(existing_sub, &view->subsurfaces, link)
    {
        if (existing_sub->wlr_subsurface == wlr_subsurface)
        {
            return existing_sub;
        }
    }

    Core *instance = Core::instance();
    SparrowSubSurface *sub = new SparrowSubSurface();
    if (!sub)
    {
        return nullptr;
    }

    sub->parent_view    = view;
    sub->parent_handle  = view->handle;
    sub->wlr_subsurface = wlr_subsurface;
    sub->surface = wlr_subsurface->surface;

    // Add to handle map for texture lookup
    sub->handle =
        handle_map_add(instance->subsurfaces, static_cast<void*>(sub));

    // Add to parent's subsurface list
    wl_list_insert(&view->subsurfaces, &sub->link);

    // Setup listeners
    sub->map.notify = sparrow_subsurface_handle_map;
    wl_signal_add(&wlr_subsurface->surface->events.map, &sub->map);

    sub->unmap.notify = sparrow_subsurface_handle_unmap;
    wl_signal_add(&wlr_subsurface->surface->events.unmap, &sub->unmap);

    sub->destroy.notify = sparrow_subsurface_handle_destroy;
    wl_signal_add(&wlr_subsurface->events.destroy, &sub->destroy);

    sub->commit.notify = sparrow_subsurface_handle_commit;
    wl_signal_add(&wlr_subsurface->surface->events.commit, &sub->commit);

    // Listen for nested child subsurfaces
    sub->new_subsurface.notify = sparrow_subsurface_handle_new_on_subsurface;
    wl_signal_add(&wlr_subsurface->surface->events.new_subsurface,
        &sub->new_subsurface);

    // Check if this subsurface already has children
    struct wlr_subsurface *child_sub;
    wl_list_for_each(child_sub,
        &wlr_subsurface->surface->current.subsurfaces_below,
        current.link)
    {
        sparrow_subsurface_create(view, child_sub);
    }
    wl_list_for_each(child_sub,
        &wlr_subsurface->surface->current.subsurfaces_above,
        current.link)
    {
        sparrow_subsurface_create(view, child_sub);
    }

    // If already mapped, initialize map
    if (wlr_subsurface->surface->mapped)
    {
        sparrow_subsurface_handle_map(&sub->map, nullptr);
    }

    return sub;
}

void sparrow_subsurface_handle_new(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, new_subsurface);
    struct wlr_subsurface *wlr_subsurface =
        static_cast<struct wlr_subsurface*>(data);
    wlr_log(WLR_INFO, "New subsurface for view %d", view->handle);
    sparrow_subsurface_create(view, wlr_subsurface);
}
