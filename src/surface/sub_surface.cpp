#include "flutter/platform/engine/messages/sub_surface_message.hpp"
#include "flutter_embedder.h"
#include <mutex>

#include "core.hpp"
#include "layer_surface.hpp"
#include "output.hpp"
#include "sub_surface.hpp"
#include "surface.hpp"
#include "util/handle_map.hpp"
#include "util/udmabuf.hpp"
#include "view.hpp"

static Output *get_subsurface_output(const SparrowSubSurface *sub)
{
    if (sub)
    {
        if (sub->parent_view && sub->parent_view->current_output)
        {
            return sub->parent_view->current_output;
        }

        if (sub->parent_layer_surface && sub->parent_layer_surface->current_output)
        {
            return sub->parent_layer_surface->current_output;
        }
    }

    return sparrow_get_first_output();
}

void sparrow_sub_surface_get_root_coords(SparrowSubSurface *sub, int *root_x,
    int *root_y)
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
             (curr->parent == sub->parent_view->xdg_surface->surface)) ||
            (sub->parent_layer_surface &&
             sub->parent_layer_surface->layer_surface &&
             (curr->parent == sub->parent_layer_surface->layer_surface->surface)))
        {
            break;
        }

        curr = wlr_subsurface_try_from_wlr_surface(curr->parent);
    }

    if (sub && sub->parent_view)
    {
        x -= sub->parent_view->geo_x;
        y -= sub->parent_view->geo_y;
    }

    *root_x = x;
    *root_y = y;
}

static void sparrow_subsurface_handle_map(struct wl_listener *listener,
    void *data)
{
    SparrowSubSurface *sub = wl_container_of(listener, sub, map);
    const auto instance    = Core::instance();

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

    if (sub->surface && (sub->parent_view || sub->parent_layer_surface))
    {
        Output *out = get_subsurface_output(sub);
        if (out && out->wlr_output)
        {
            wlr_surface_send_enter(sub->surface, out->wlr_output);
        }
    }

    // Register external texture
    // Offset subsurface texture IDs by 100000 to avoid collision with view
    // texture IDs
    sub->texture_id = 100000LL + sub->handle;
    if (!sub->texture_registered && instance->engine)
    {
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
    }

    // Send to Dart
    // instance->callable_queue.enqueue([=] {
    send_subsurface_map(sub);
    // });
}

static void sparrow_subsurface_handle_unmap(struct wl_listener *listener,
    void *data)
{
    SparrowSubSurface *sub = wl_container_of(listener, sub, unmap);
    uint32_t handle = sub->handle;
    uint32_t parent_handle = sub->parent_handle;

    wlr_log(WLR_INFO, "Subsurface UNMAP: handle=%d", sub->handle);

    // Send to Dart
    send_subsurface_unmap(handle, parent_handle);
}

void sparrow_subsurface_damage_add_rect(SparrowSubSurface *sub, int x, int y,
    int width, int height, bool schedule_output_frame)
{
    if (!sub || !sub->parent_view)
    {
        if (!sub || (!sub->parent_view && !sub->parent_layer_surface))
        {
            return;
        }
    }

    if ((width <= 0) || (height <= 0))
    {
        return;
    }

    const auto instance = Core::instance();
    if (instance && instance->force_render_all_views)
    {
        if (schedule_output_frame)
        {
            sparrow_damage_add_box(nullptr, false, true);
        }

        return;
    }

    if (sub->parent_layer_surface)
    {
        if (sparrow_layer_surface_is_occluded(sub->parent_layer_surface,
            sub->parent_layer_surface->current_output))
        {
            return;
        }

        struct wlr_box box = {
            .x     = sub->parent_layer_surface->x + sub->x + x,
            .y     = sub->parent_layer_surface->y + sub->y + y,
            .width = width,
            .height = height,
        };
        sparrow_damage_add_box(&box, true, schedule_output_frame);
        return;
    }

    SparrowView *view = sub->parent_view;
    Output *output    =
        view->current_output ? view->current_output : sparrow_get_first_output();
    if (!output || !output->wlr_output)
    {
        return;
    }

    struct wlr_box scene_box = {};
    int mapped_x = 0, mapped_y = 0, mapped_w = 0, mapped_h = 0;
    if (sparrow_view_get_scene_box(view, &scene_box))
    {
        const int base_x = scene_box.x;
        const int base_y = scene_box.y;
        if ((scene_box.width > 0) && (view->width > 0) &&
            ((scene_box.width != view->width) ||
             (scene_box.height != view->height)))
        {
            const double scale_x = (double)scene_box.width / (double)view->width;
            const double scale_y = (double)scene_box.height / (double)view->height;
            mapped_x = base_x + (int)lround((sub->x + x) * scale_x);
            mapped_y = base_y + (int)lround((sub->y + y) * scale_y);
            mapped_w = (int)lround(width * scale_x);
            mapped_h = (int)lround(height * scale_y);
        } else
        {
            mapped_x = base_x + (sub->x + x);
            mapped_y = base_y + (sub->y + y);
            mapped_w = width;
            mapped_h = height;
        }
    } else
    {
        mapped_x = view->x + (sub->x + x);
        mapped_y = view->y + (sub->y + y);
        mapped_w = width;
        mapped_h = height;
    }

    struct wlr_box box = {
        .x     = mapped_x,
        .y     = mapped_y,
        .width = mapped_w,
        .height = mapped_h,
    };
    sparrow_damage_add_box(&box, true, schedule_output_frame);
}

static void sparrow_subsurface_handle_commit(struct wl_listener *listener,
    void *data)
{
    SparrowSubSurface *sub = wl_container_of(listener, sub, commit);
    const auto instance    = Core::instance();

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
            wlr_log(
                WLR_DEBUG,
                "Subsurface %d sending position update: %dx%d (buf=%dx%d) @ (%d,%d)",
                sub->handle, sub->width, sub->height, sub->buffer_width,
                sub->buffer_height, sub->x, sub->y);
        }

        sparrow_subsurface_send_position(sub);
        if (sub->parent_view && sub->parent_view->activated)
        {
            const bool in_direct =
                sparrow_view_is_in_direct_mode(sub->parent_view);
            if ((old_w > 0) && (old_h > 0))
            {
                sparrow_subsurface_damage_add_rect(sub, 0, 0, old_w, old_h,
                    in_direct);
            }

            sparrow_subsurface_damage_add_rect(sub, 0, 0, sub->width, sub->height,
                in_direct);
        }
    }

    bool new_buffer_attached = false;
    {
        std::lock_guard<std::recursive_mutex> lock(
            instance->sparrow_renderer.texture_mutex);
        struct wlr_buffer *new_buf = nullptr;
        struct wlr_buffer *committed_buf = nullptr;
        bool has_committed_buffer = false;
        if (sub->surface)
        {
            has_committed_buffer =
                (sub->surface->current.committed & WLR_SURFACE_STATE_BUFFER) != 0;
            committed_buf = sub->surface->current.buffer;
            if ((committed_buf == nullptr) && sub->surface->buffer)
            {
                committed_buf = sub->surface->buffer->source;
            }
        }

        struct wlr_dmabuf_attributes dmabuf_attrs = {};
        if ((committed_buf != nullptr) &&
            wlr_buffer_get_dmabuf(committed_buf, &dmabuf_attrs))
        {
            new_buf = committed_buf;
        }

        new_buffer_attached = has_committed_buffer && (committed_buf != nullptr);
        if ((has_committed_buffer || (sub->locked_buffer == nullptr)) &&
            (new_buf != sub->locked_buffer))
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

        if (new_buffer_attached)
        {
            struct timespec ts_commit;
            clock_gettime(CLOCK_MONOTONIC, &ts_commit);
            uint64_t commit_now_us =
                (uint64_t)ts_commit.tv_sec * 1000000ULL + (ts_commit.tv_nsec / 1000);
            double commit_dt_ms = (sub->last_commit_time_us > 0) ?
                (double)(commit_now_us - sub->last_commit_time_us) / 1000.0 :
                0.0;
            sub->last_commit_time_us = commit_now_us;
            sub->commit_count++;

            if (!sub->current_buffer_sampled && (sub->commit_count > 1))
            {
                sub->dropped_buffer_count++;
                if (instance->debug_pacing)
                {
                    const char *parent_app = "unknown";
                    if (sub->parent_view && sub->parent_view->toplevel &&
                        sub->parent_view->toplevel->app_id)
                    {
                        parent_app = sub->parent_view->toplevel->app_id;
                    } else if (sub->parent_layer_surface &&
                               sub->parent_layer_surface->layer_surface &&
                               sub->parent_layer_surface->layer_surface->namespace_t)
                    {
                        parent_app =
                            sub->parent_layer_surface->layer_surface->namespace_t;
                    }

                    wlr_log(WLR_INFO,
                        "[PACING-DROP-SUB] Subsurface %u ('%s') committed before previous buffer sampled! "
                        "(dropped: %lu, commits: %lu, dt: %.2fms)",
                        sub->handle, parent_app,
                        (unsigned long)sub->dropped_buffer_count,
                        (unsigned long)sub->commit_count, commit_dt_ms);
                }
            }

            sub->current_buffer_sampled = false;
        }
    }

    // Fallback: if subsurface is mapped but texture isn't registered, do it now
    if (sub->surface && sub->surface->mapped && !sub->texture_registered &&
        instance->engine)
    {
        sub->texture_id = 100000LL + sub->handle;
        FlutterEngineResult result = instance->embedder_api.RegisterExternalTexture(
            instance->engine, sub->texture_id);
        if (result == kSuccess)
        {
            sub->texture_registered = true;
            wlr_log(
                WLR_INFO,
                "Registered subsurface texture via commit fallback: %ld (handle=%d)",
                sub->texture_id, sub->handle);
            send_subsurface_map(sub);
            sparrow_subsurface_send_position(sub);
        }
    }

    // Notify Flutter and add damage for any mapped subsurface
    if (sub->surface && sub->surface->mapped)
    {
        if (sub->parent_view || sub->parent_layer_surface)
        {
            Output *out = get_subsurface_output(sub);
            if (out && out->wlr_output)
            {
                wlr_surface_send_enter(sub->surface, out->wlr_output);
            }
        }

        const bool layer_mapped = sub->parent_layer_surface &&
            sub->parent_layer_surface->layer_surface &&
            sub->parent_layer_surface->layer_surface->surface &&
            sub->parent_layer_surface->layer_surface->surface->mapped;
        const bool is_visible = sub->parent_view ?
            sparrow_view_is_visible(sub->parent_view) :
            (sub->parent_layer_surface ?
                (layer_mapped &&
                    !sparrow_layer_surface_is_occluded(sub->parent_layer_surface,
                        sub->parent_layer_surface->current_output)) :
                true);
        const bool in_direct_mode =
            sub->parent_view && sparrow_view_is_in_direct_mode(sub->parent_view);

        pixman_region32_t damage;
        pixman_region32_init(&damage);
        wlr_surface_get_effective_damage(sub->surface, &damage);
        const bool has_damage = pixman_region32_not_empty(&damage);

        // Notify Flutter that texture has new frame ONLY when not bypassed by
        // fullscreen Direct Scanout / Direct 1-Pass Blit.
        if (is_visible && !in_direct_mode && sub->texture_registered &&
            (new_buffer_attached || has_damage))
        {
            instance->embedder_api.MarkExternalTextureFrameAvailable(instance->engine,
                sub->texture_id);
        }

        if (has_damage)
        {
            sub->current_buffer_sampled = false;
            if (is_visible)
            {
                if (instance->show_fps)
                {
                    struct timespec ts;
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    uint64_t now_us =
                        (uint64_t)ts.tv_sec * 1000000ULL + (ts.tv_nsec / 1000);
                    instance->record_client_commit(now_us);
                }

                if (sub->parent_view)
                {
                    sparrow_notify_redraw_activity(sub->parent_view, &damage);
                }

                // In Tier 3 (Flutter compositing), present_layers adds output damage
                // and schedules the KMS flip AFTER Flutter renders the updated
                // external texture into page_texture. Damaging the output here before
                // Flutter renders would trigger a premature KMS flip with the stale
                // page_texture (causing 2 KMS flips per client commit = 30 FPS cap).
                if (in_direct_mode || instance->debug_damage)
                {
                    int nrects = 0;
                    pixman_box32_t *rects =
                        pixman_region32_rectangles(&damage, &nrects);
                    for (int i = 0; i < nrects; ++i)
                    {
                        sparrow_subsurface_damage_add_rect(sub, rects[i].x1,
                            rects[i].y1, rects[i].x2 - rects[i].x1,
                            rects[i].y2 - rects[i].y1, in_direct_mode);
                    }
                } else if (sub->parent_layer_surface)
                {
                    sparrow_damage_add_box(nullptr, false);
                }
            }
        } else if (is_visible && new_buffer_attached)
        {
            if (sub->parent_view)
            {
                sparrow_notify_redraw_activity(sub->parent_view, nullptr);
                if (in_direct_mode)
                {
                    sparrow_subsurface_damage_add_rect(sub, 0, 0, sub->width,
                        sub->height);
                }
            } else if (sub->parent_layer_surface)
            {
                sparrow_damage_add_box(nullptr, false);
            }
        }

        pixman_region32_fini(&damage);

        // If this commit requested a wl_surface.frame callback while in Tier 3
        // (where we do not schedule an immediate output_frame), ensure the
        // VBlank frame_done path is scheduled if no KMS pageflip is in flight.
        if (is_visible && !in_direct_mode &&
            !wl_list_empty(&sub->surface->current.frame_callback_list))
        {
            Output *out = get_subsurface_output(sub);
            if (out && out->wlr_output && out->wlr_output->enabled &&
                !out->wlr_output->frame_pending)
            {
                wlr_output_schedule_frame(out->wlr_output);
            }
        }
    }
}

static void sparrow_subsurface_handle_destroy(struct wl_listener *listener,
    void *data)
{
    SparrowSubSurface *sub = wl_container_of(listener, sub, destroy);
    const auto instance    = Core::instance();

    wlr_log(WLR_INFO, "Subsurface DESTROY: handle=%d", sub->handle);

    {
        std::lock_guard<std::recursive_mutex> lock(
            instance->sparrow_renderer.texture_mutex);

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
    }

    delete sub;
}

static void sparrow_subsurface_handle_new_on_subsurface(struct wl_listener *listener,
    void *data)
{
    SparrowSubSurface *parent_sub =
        wl_container_of(listener, parent_sub, new_subsurface);
    struct wlr_subsurface *child_wlr_subsurface =
        static_cast<struct wlr_subsurface*>(data);
    if (!parent_sub)
    {
        return;
    }

    if (parent_sub->parent_view)
    {
        wlr_log(WLR_INFO, "New nested subsurface for view %d (parent sub=%d)",
            parent_sub->parent_view->handle, parent_sub->handle);
        sparrow_subsurface_create(parent_sub->parent_view, child_wlr_subsurface);
    } else if (parent_sub->parent_layer_surface)
    {
        wlr_log(WLR_INFO,
            "New nested subsurface for layer surface %d (parent sub=%d)",
            parent_sub->parent_layer_surface->handle, parent_sub->handle);
        sparrow_subsurface_create_layer(parent_sub->parent_layer_surface,
            child_wlr_subsurface);
    }
}

static SparrowSubSurface *sparrow_subsurface_create_for_parent(
    SparrowView *view, SparrowLayerSurface *layer_surf,
    struct wlr_subsurface *wlr_subsurface)
{
    if (((view == nullptr) && (layer_surf == nullptr)) ||
        (wlr_subsurface == nullptr) || (wlr_subsurface->surface == nullptr))
    {
        return nullptr;
    }

    struct wl_list *parent_list =
        view ? &view->subsurfaces : &layer_surf->subsurfaces;

    // Avoid duplicate creation if already registered
    SparrowSubSurface *existing_sub;
    wl_list_for_each(existing_sub, parent_list, link)
    {
        if (existing_sub->wlr_subsurface == wlr_subsurface)
        {
            return existing_sub;
        }
    }

    const auto instance    = Core::instance();
    SparrowSubSurface *sub = new SparrowSubSurface();
    if (!sub)
    {
        return nullptr;
    }

    sub->parent_view = view;
    sub->parent_layer_surface = layer_surf;
    sub->parent_handle = view ?
        view->handle :
        (kLayerSubsurfaceParentOffset + layer_surf->handle);
    sub->wlr_subsurface = wlr_subsurface;
    sub->surface = wlr_subsurface->surface;

    // Add to handle map for texture lookup
    sub->handle = handle_map_add(instance->subsurfaces, static_cast<void*>(sub));

    // Add to parent's subsurface list
    wl_list_insert(parent_list, &sub->link);

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
        sparrow_subsurface_create_for_parent(view, layer_surf, child_sub);
    }
    wl_list_for_each(child_sub,
        &wlr_subsurface->surface->current.subsurfaces_above,
        current.link)
    {
        sparrow_subsurface_create_for_parent(view, layer_surf, child_sub);
    }

    // If already mapped, initialize map
    if (wlr_subsurface->surface->mapped)
    {
        sparrow_subsurface_handle_map(&sub->map, nullptr);
    }

    return sub;
}

SparrowSubSurface *sparrow_subsurface_create(SparrowView *view,
    struct wlr_subsurface *wlr_subsurface)
{
    return sparrow_subsurface_create_for_parent(view, nullptr, wlr_subsurface);
}

SparrowSubSurface *sparrow_subsurface_create_layer(
    SparrowLayerSurface *layer_surf, struct wlr_subsurface *wlr_subsurface)
{
    return sparrow_subsurface_create_for_parent(nullptr, layer_surf,
        wlr_subsurface);
}

void sparrow_subsurface_handle_new(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, new_subsurface);
    struct wlr_subsurface *wlr_subsurface =
        static_cast<struct wlr_subsurface*>(data);
    wlr_log(WLR_INFO, "New subsurface for view %d", view->handle);
    sparrow_subsurface_create(view, wlr_subsurface);
}

void sparrow_subsurface_handle_new_layer(struct wl_listener *listener,
    void *data)
{
    SparrowLayerSurface *layer_surf =
        wl_container_of(listener, layer_surf, new_subsurface);
    struct wlr_subsurface *wlr_subsurface =
        static_cast<struct wlr_subsurface*>(data);
    wlr_log(WLR_INFO, "New subsurface for layer surface %d",
        layer_surf->handle);
    sparrow_subsurface_create_layer(layer_surf, wlr_subsurface);
}
