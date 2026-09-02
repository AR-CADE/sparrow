#include "view.hpp"
#include "core.hpp"
#include "input/pointer.hpp"
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

    Output *output = view->current_output ? view->current_output : sparrow_get_first_output();
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

    const int out_w    = output->wlr_output->width;
    const int out_h    = output->wlr_output->height;
    struct wlr_box box = {
        .x     = view->x,
        .y     = view->y,
        .width = out_w,
        .height = out_h,
    };
    sparrow_damage_add_box(&box, true);
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

    Output *output = view->current_output ? view->current_output : sparrow_get_first_output();
    if (!output || !output->wlr_output)
    {
        return;
    }

    const int out_w = output->wlr_output->width;
    const int out_h = output->wlr_output->height;

    // If view dimensions (visW, visH) differ from container/output size (e.g. CPU-X),
    // Flutter SurfaceView scales with uniform aspect-ratio and centers with black bars:
    // scale = min(out_w / vis_w, out_h / vis_h)
    // target_w = vis_w * scale, target_h = vis_h * scale
    // black_bar_x = (out_w - target_w) / 2, black_bar_y = (out_h - target_h) / 2
    const int vis_w = view->width > 0 ? view->width : out_w;
    const int vis_h = view->height > 0 ? view->height : out_h;
    const double scale_x = (vis_w > 0) ? ((double)out_w / (double)vis_w) : 1.0;
    const double scale_y = (vis_h > 0) ? ((double)out_h / (double)vis_h) : 1.0;
    const double scale   = (scale_x < scale_y) ? scale_x : scale_y;

    const int target_w    = (int)lround(vis_w * scale);
    const int target_h    = (int)lround(vis_h * scale);
    const int black_bar_x = (out_w - target_w) / 2;
    const int black_bar_y = (out_h - target_h) / 2;

    const int mapped_x = (int)lround(view->x + black_bar_x + (x - view->geo_x) * scale);
    const int mapped_y = (int)lround(view->y + black_bar_y + (y - view->geo_y) * scale);
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

bool sparrow_view_get_scene_box(const SparrowView *view, struct wlr_box *out_box)
{
    Core *instance = Core::instance();

    if (!view || !instance)
    {
        return false;
    }

    struct sparrow_renderer *renderer = &instance->sparrow_renderer;

    pthread_mutex_lock(&renderer->render_mutex);
    bool found = false;
    struct wlr_box box = {};

    for (int i = 0; i < (int)renderer->current_scene.layers_count; i++)
    {
        const struct sparrow_renderer_scene_layer *layer = &renderer->current_scene.layers[i];
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

    pthread_mutex_unlock(&renderer->render_mutex);

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

    if (!view || !instance || !damage ||
        !pixman_region32_not_empty(damage))
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

    pthread_mutex_lock(&renderer->render_mutex);
    int view_layer_idx = -1;
    for (int i = 0; i < (int)renderer->current_scene.layers_count; i++)
    {
        struct sparrow_renderer_scene_layer *layer = &renderer->current_scene.layers[i];
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

    pthread_mutex_unlock(&renderer->render_mutex);

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

void sparrow_view_focus(SparrowView *view)
{
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
        return;
    }

    if (prev_surface)
    {
        struct wlr_xdg_surface *previous = wlr_xdg_surface_try_from_wlr_surface(
            seat->keyboard_state.focused_surface);
        if (previous && (previous->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL))
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

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
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

    double sx, sy;
    const struct wlr_scene_node *node =
        wlr_scene_node_at(&instance->scene->tree.node, instance->cursor->x,
                          instance->cursor->y, &sx, &sy);

    if (node)
    {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_frame(seat);
    } else
    {
        wlr_seat_pointer_notify_enter(seat, surface, 0, 0);
        wlr_seat_pointer_notify_frame(seat);
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
        if (sub && sub->texture_registered)
        {
            instance->embedder_api.MarkExternalTextureFrameAvailable(instance->engine,
                sub->texture_id);
        }
    }
    sparrow_view_damage_whole(view);
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
