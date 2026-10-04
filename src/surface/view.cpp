#include "view.hpp"
#include <mutex>
#include "core.hpp"
#include "flutter/platform/text_input.hpp"
#include "input/pointer.hpp"
#include "output.hpp"
#include "session_lock.hpp"
#include "sub_surface.hpp"
void sparrow_view_damage_whole(SparrowView *view)
{
    if (!view)
    {
        return;
    }

    Core *instance = Core::instance();
    if (instance && instance->force_render_all_views)
    {
        sparrow_damage_add_box(nullptr, false);
        return;
    }

    Output *output =
        view->current_output ? view->current_output : sparrow_get_first_output();
    if (!output || !output->wlr_output)
    {
        if ((view->width > 0) && (view->height > 0))
        {
            struct wlr_box box = {
                .x     = view->x,
                .y     = view->y,
                .width = view->width,
                .height = view->height,
            };
            sparrow_damage_add_box(&box, true);
        }

        return;
    }

    // If the view had a previous scene box that differs from current, damage it too
    // so background/underlying layers get properly redrawn
    if ((view->last_scene_box.width > 0) && (view->last_scene_box.height > 0))
    {
        sparrow_damage_add_box(&view->last_scene_box, true);
    }

    int out_w = 0, out_h = 0;
    wlr_output_effective_resolution(output->wlr_output, &out_w, &out_h);

    struct wlr_box box = {};
    if (view->maximized || view->fullscreen)
    {
        box.x     = 0;
        box.y     = 0;
        box.width = out_w;
        box.height = out_h;
    } else if (!sparrow_view_get_scene_box(view, &box))
    {
        box.x     = view->x;
        box.y     = view->y;
        box.width = (view->width > 0) ? view->width : out_w;
        box.height = (view->height > 0) ? view->height : out_h;
    }

    sparrow_damage_add_box(&box, true);
    view->last_scene_box = box;
}

void sparrow_view_damage_add_rect(SparrowView *view, int x, int y, int width,
    int height)
{
    if (!view)
    {
        return;
    }

    if ((width <= 0) || (height <= 0))
    {
        sparrow_view_damage_whole(view);
        return;
    }

    Core *instance = Core::instance();
    if (instance && instance->force_render_all_views)
    {
        // In overview mode: views are miniature cards in the overview grid.
        // Schedule frame presentation without drawing 1:1 window-scale boxes.
        sparrow_damage_add_box(nullptr, false);
        return;
    }

    Output *output =
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
            ((scene_box.width != view->width) || (scene_box.height != view->height)))
        {
            const double scale_x = (double)scene_box.width / (double)view->width;
            const double scale_y = (double)scene_box.height / (double)view->height;
            mapped_x = base_x + (int)lround((x - view->geo_x) * scale_x);
            mapped_y = base_y + (int)lround((y - view->geo_y) * scale_y);
            mapped_w = (int)lround(width * scale_x);
            mapped_h = (int)lround(height * scale_y);
        } else
        {
            mapped_x = base_x + (x - view->geo_x);
            mapped_y = base_y + (y - view->geo_y);
            mapped_w = width;
            mapped_h = height;
        }
    } else
    {
        mapped_x = view->x + (x - view->geo_x);
        mapped_y = view->y + (y - view->geo_y);
        mapped_w = width;
        mapped_h = height;
    }

    struct wlr_box box = {
        .x     = mapped_x,
        .y     = mapped_y,
        .width = mapped_w,
        .height = mapped_h,
    };
    sparrow_damage_add_box(&box, true);
}

bool sparrow_view_get_scene_box(const SparrowView *view,
    struct wlr_box *out_box)
{
    Core *instance = Core::instance();

    if (!view || !instance)
    {
        return false;
    }

    struct sparrow_renderer *renderer = &instance->sparrow_renderer;

    bool found = false;
    struct wlr_box box = {};
    {
        std::lock_guard<std::recursive_mutex> lock(renderer->render_mutex);

        for (int i = 0; i < (int)renderer->current_scene.layers_count; i++)
        {
            const struct sparrow_renderer_scene_layer *layer =
                &renderer->current_scene.layers[i];
            if ((layer->type == sceneLayerPlatform) &&
                (layer->platform.platform_view_id == view->handle))
            {
                box.x     = (int)lround(layer->offset.x);
                box.y     = (int)lround(layer->offset.y);
                box.width = (int)lround(layer->size.width);
                box.height = (int)lround(layer->size.height);
                found = true;
                break;
            }
        }
    }

    if (!found)
    {
        return false;
    }

    // Check if box intersects ANY enabled output in output_layout
    bool on_screen = false;
    Output *output;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (!output->wlr_output || !output->wlr_output->enabled)
        {
            continue;
        }

        struct wlr_box out_box_geom;
        wlr_output_layout_get_box(instance->output_layout, output->wlr_output,
            &out_box_geom);
        struct wlr_box intersection;
        if (wlr_box_intersection(&intersection, &out_box_geom, &box))
        {
            if ((intersection.width > 0) && (intersection.height > 0))
            {
                on_screen = true;
                break;
            }
        }
    }

    if (!on_screen)
    {
        return false;
    }

    if (out_box)
    {
        *out_box = box;
    }

    return true;
}

bool sparrow_view_filter_occluded_damage(
    const SparrowView *view, pixman_region32_t *damage,
    pixman_region32_t *visible_damage_out)
{
    Core *instance = Core::instance();

    if (!view || !instance || !damage || !pixman_region32_not_empty(damage))
    {
        return false;
    }

    struct sparrow_renderer *renderer = &instance->sparrow_renderer;

    // 1. If the view is not in Flutter's active scene (e.g. on another PageView
    // page / workspace) or completely scrolled off-screen, it is 100% hidden ->
    // skip redraw!
    struct wlr_box scene_box = {};
    if (!sparrow_view_get_scene_box(view, &scene_box))
    {
        return false;
    }

    pixman_region32_t opaque_above;
    pixman_region32_init(&opaque_above);

    {
        std::lock_guard<std::recursive_mutex> lock(renderer->render_mutex);
        int view_layer_idx = -1;
        for (int i = 0; i < (int)renderer->current_scene.layers_count; i++)
        {
            struct sparrow_renderer_scene_layer *layer =
                &renderer->current_scene.layers[i];
            if ((layer->type == sceneLayerPlatform) &&
                (layer->platform.platform_view_id == view->handle))
            {
                view_layer_idx = i;
                break;
            }
        }

        if (view_layer_idx >= 0)
        {
            // Layers with index > view_layer_idx are stacked ON TOP of this view in
            // Flutter
            for (int j = view_layer_idx + 1;
                 j < (int)renderer->current_scene.layers_count; j++)
            {
                struct sparrow_renderer_scene_layer *top_layer =
                    &renderer->current_scene.layers[j];
                if (top_layer->type == sceneLayerPlatform)
                {
                    uint32_t top_handle   = top_layer->platform.platform_view_id;
                    SparrowView *top_view = instance->find_view_by_handle(top_handle);
                    if (top_view && top_view->xdg_surface &&
                        top_view->xdg_surface->surface)
                    {
                        struct wlr_surface *top_surf = top_view->xdg_surface->surface;
                        const int top_x = (int)lround(top_layer->offset.x);
                        const int top_y = (int)lround(top_layer->offset.y);
                        if (pixman_region32_not_empty(&top_surf->current.opaque))
                        {
                            pixman_region32_t top_op;
                            pixman_region32_init(&top_op);
                            pixman_region32_copy(&top_op, &top_surf->current.opaque);
                            pixman_region32_translate(&top_op, top_x, top_y);
                            pixman_region32_union(&opaque_above, &opaque_above, &top_op);
                            pixman_region32_fini(&top_op);
                        } else if (top_view->maximized || top_view->fullscreen)
                        {
                            // Maximized/fullscreen windows are considered fully opaque over
                            // their bounds
                            pixman_region32_t top_op;
                            int tw = (int)lround(top_layer->size.width);
                            int th = (int)lround(top_layer->size.height);
                            if ((tw > 0) && (th > 0))
                            {
                                pixman_region32_init_rect(&top_op, top_x, top_y, tw, th);
                                pixman_region32_union(&opaque_above, &opaque_above, &top_op);
                                pixman_region32_fini(&top_op);
                            }
                        }
                    }
                }
            }
        }
    }

    if (pixman_region32_not_empty(&opaque_above))
    {
        pixman_region32_subtract(visible_damage_out, damage, &opaque_above);
    } else
    {
        pixman_region32_copy(visible_damage_out, damage);
    }

    pixman_region32_fini(&opaque_above);
    return pixman_region32_not_empty(visible_damage_out);
}

bool sparrow_view_contains_surface(const SparrowView *view,
    const struct wlr_surface *surface)
{
    if (!view || !view->xdg_surface || !surface)
    {
        return false;
    }

    if (view->xdg_surface->surface == surface)
    {
        return true;
    }

    SparrowSubSurface *sub = nullptr;
    wl_list_for_each(sub, &view->subsurfaces, link)
    {
        if (sub->surface == surface)
        {
            return true;
        }
    }
    return false;
}

void sparrow_view_focus(SparrowView *view)
{
    if (sparrow_is_session_locked())
    {
        return;
    }

    if (view == nullptr)
    {
        return;
    }

    Core *instance = Core::instance();
    struct wlr_seat *seat = instance->seat;
    struct wlr_surface *surface = view->xdg_surface->surface;
    const struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    if (prev_surface == surface)
    {
        if (view->xdg_surface && view->xdg_surface->initialized &&
            (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) &&
            view->toplevel)
        {
            if (!view->activated)
            {
                wlr_xdg_toplevel_set_activated(view->toplevel, true);
                view->activated = true;
                if (view->foreign_toplevel)
                {
                    wlr_foreign_toplevel_handle_v1_set_activated(view->foreign_toplevel,
                        true);
                }
            }
        }

        if (!sparrow_view_contains_surface(view,
            seat->pointer_state.focused_surface))
        {
            double sx, sy;
            const struct wlr_scene_node *node =
                wlr_scene_node_at(&instance->scene->tree.node, instance->cursor->x,
                                  instance->cursor->y, &sx, &sy);
            if (node)
            {
                wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
                wlr_seat_pointer_notify_frame(seat);
            }
        }

        sparrow_pointer_constraints_set_focus(instance, surface);

        if (view->texture_registered)
        {
            instance->embedder_api.MarkExternalTextureFrameAvailable(
                instance->engine, view->texture_id);
        }

        return;
    }

    if (prev_surface)
    {
        struct wlr_xdg_surface *previous = wlr_xdg_surface_try_from_wlr_surface(
            seat->keyboard_state.focused_surface);
        if (previous && previous->initialized &&
            (previous->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) &&
            previous->toplevel)
        {
            wlr_xdg_toplevel_set_activated(previous->toplevel, false);
            SparrowView *prev_view = static_cast<SparrowView*>(previous->data);
            if (prev_view != nullptr)
            {
                SparrowView *v = instance->find_view_by_handle(prev_view->handle);
                if (v)
                {
                    v->activated = false;
                    if (v->foreign_toplevel)
                    {
                        wlr_foreign_toplevel_handle_v1_set_activated(v->foreign_toplevel,
                            false);
                    }
                }
            }
        }
    }

    // Ensure all other mapped views are deactivated
    SparrowView *other_view = nullptr;
    wl_list_for_each(other_view, &instance->views_list, link)
    {
        if ((other_view != view) && other_view->activated)
        {
            other_view->activated = false;
            if (other_view->toplevel)
            {
                wlr_xdg_toplevel_set_activated(other_view->toplevel, false);
            }

            if (other_view->foreign_toplevel)
            {
                wlr_foreign_toplevel_handle_v1_set_activated(
                    other_view->foreign_toplevel, false);
            }
        }
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    sparrow_text_input_stop_repeat(0);
    if (view->xdg_surface && view->xdg_surface->initialized &&
        (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) &&
        view->toplevel)
    {
        wlr_xdg_toplevel_set_activated(view->toplevel, true);
        view->activated = true;
        if (view->foreign_toplevel)
        {
            wlr_foreign_toplevel_handle_v1_set_activated(view->foreign_toplevel,
                true);
        }
    }

    if (keyboard != nullptr)
    {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
            keyboard->num_keycodes,
            &keyboard->modifiers);
    } else
    {
        struct wlr_keyboard_modifiers modifiers = {};
        wlr_seat_keyboard_notify_enter(seat, surface, nullptr, 0, &modifiers);
    }

    if (!sparrow_view_contains_surface(view,
        seat->pointer_state.focused_surface))
    {
        double sx, sy;
        const struct wlr_scene_node *node =
            wlr_scene_node_at(&instance->scene->tree.node, instance->cursor->x,
                              instance->cursor->y, &sx, &sy);

        if (node)
        {
            wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
            wlr_seat_pointer_notify_frame(seat);
        }
    }

    sparrow_pointer_constraints_set_focus(instance, surface);

    // Wake up Flutter texture and repaint with the latest frame when view gets
    // focused
    if (view->texture_registered)
    {
        instance->embedder_api.MarkExternalTextureFrameAvailable(instance->engine,
            view->texture_id);
    }

    SparrowSubSurface *sub = nullptr;
    wl_list_for_each(sub, &view->subsurfaces, link)
    {
        if (sub->texture_registered)
        {
            instance->embedder_api.MarkExternalTextureFrameAvailable(instance->engine,
                sub->texture_id);
        }
    }
    sparrow_view_damage_whole(view);
}

bool sparrow_view_is_visible(const SparrowView *view)
{
    if (sparrow_is_session_locked())
    {
        return false;
    }

    if (!view || !view->xdg_surface || !view->xdg_surface->surface ||
        !view->xdg_surface->surface->mapped)
    {
        return false;
    }

    Core *instance = Core::instance();
    if (!instance)
    {
        return false;
    }

    // 1. Overview mode: all mapped views are visible in the overview grid
    if (instance->force_render_all_views)
    {
        return true;
    }

    // 2. Active scene box (platform view if present in current scene)
    struct wlr_box scene_box = {};
    if (sparrow_view_get_scene_box(view, &scene_box))
    {
        return true;
    }

    // 3. Normal mode: if this view is activated (the current focused page), it is
    // visible
    if (view->activated)
    {
        return true;
    }

    // 4. Single-view fallback: if there is only 1 mapped view in the compositor,
    // it is on the only page, so it is visible even if focus is temporarily
    // cleared
    size_t mapped_view_count     = 0;
    const SparrowView *only_view = nullptr;
    const SparrowView *v = nullptr;
    wl_list_for_each(v, &instance->views_list, link)
    {
        if (v->xdg_surface && v->xdg_surface->surface &&
            v->xdg_surface->surface->mapped)
        {
            mapped_view_count++;
            only_view = v;
        }
    }
    if ((mapped_view_count <= 1) && (only_view == view))
    {
        return true;
    }

    return false;
}

void sparrow_view_update_scene(SparrowView *view)
{
    if ((view->scene_tree == nullptr) || (view->scene_xdg_tree == nullptr))
    {
        return;
    }

    wlr_scene_node_set_position(&view->scene_xdg_tree->node, 0, 0);
}

void sparrow_view_create_scene(SparrowView *view)
{
    if (view->scene_tree != nullptr)
    {
        return;
    }

    Core *instance = Core::instance();
    if (!instance || (instance->scene == nullptr))
    {
        return;
    }

    view->scene_tree = wlr_scene_tree_create(&instance->scene->tree);
    view->scene_tree->node.data = view;
    wlr_scene_node_set_position(&view->scene_tree->node, 0, 0);

    view->scene_xdg_tree =
        wlr_scene_xdg_surface_create(view->scene_tree, view->xdg_surface);
    view->scene_xdg_tree->node.data = view;
    sparrow_view_update_scene(view);
}

void sparrow_view_destroy_scene(SparrowView *view)
{
    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_destroy(&view->scene_tree->node);
    }

    view->scene_tree     = nullptr;
    view->scene_xdg_tree = nullptr;
    view->scene_frame    = nullptr;
    view->scene_titlebar = nullptr;
}
