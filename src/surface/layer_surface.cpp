#include "layer_surface.hpp"
#include "sub_surface.hpp"
#include <core.hpp>
#include <output.hpp>
#include <surface/view.hpp>
#include <util/handle_map.hpp>
#include <util/udmabuf.hpp>
#include <flutter/platform/engine/messages/layer_surface_message.hpp>
#include <flutter/platform/engine/messages/sub_surface_message.hpp>
#include <surface/secure_output.hpp>

static void layer_surface_configure(SparrowLayerSurface *layer_surf)
{
    if (!layer_surf || !layer_surf->layer_surface)
    {
        return;
    }

    struct wlr_layer_surface_v1 *wlr_ls = layer_surf->layer_surface;
    if (!wlr_ls->initialized)
    {
        return;
    }

    if (!wlr_ls->output)
    {
        Output *out = sparrow_get_first_output();
        if (out && out->wlr_output)
        {
            wlr_ls->output = out->wlr_output;
            layer_surf->current_output = out;
        } else
        {
            return;
        }
    }

    int out_w = 0;
    int out_h = 0;
    wlr_output_effective_resolution(wlr_ls->output, &out_w, &out_h);
    if ((out_w <= 0) || (out_h <= 0))
    {
        out_w = wlr_ls->output->width;
        out_h = wlr_ls->output->height;
    }

    uint32_t conf_w = wlr_ls->current.desired_width ?
        wlr_ls->current.desired_width : wlr_ls->pending.desired_width;
    uint32_t conf_h = wlr_ls->current.desired_height ?
        wlr_ls->current.desired_height : wlr_ls->pending.desired_height;

    const uint32_t anchor = wlr_ls->current.anchor ?
        wlr_ls->current.anchor : wlr_ls->pending.anchor;
    const bool anchor_left   = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) != 0;
    const bool anchor_right  = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) != 0;
    const bool anchor_top    = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) != 0;
    const bool anchor_bottom = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) != 0;

    const auto & margin = (wlr_ls->current.anchor != 0 ||
        (wlr_ls->current.committed & WLR_LAYER_SURFACE_V1_STATE_MARGIN)) ?
        wlr_ls->current.margin : wlr_ls->pending.margin;

    const uint32_t layer =
        wlr_ls->current.layer ? wlr_ls->current.layer : wlr_ls->pending.layer;
    if ((layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) ||
        (layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM))
    {
        conf_w = (conf_w > 0) ? conf_w : static_cast<uint32_t>(out_w);
        conf_h = (conf_h > 0) ? conf_h : static_cast<uint32_t>(out_h);
    } else
    {
        if (anchor_left && anchor_right)
        {
            conf_w = static_cast<uint32_t>(
                std::max(0, out_w - static_cast<int>(margin.left + margin.right)));
        }

        if (anchor_top && anchor_bottom)
        {
            conf_h = static_cast<uint32_t>(
                std::max(0, out_h - static_cast<int>(margin.top + margin.bottom)));
        }
    }

    wlr_layer_surface_v1_configure(wlr_ls, conf_w, conf_h);
}

void sparrow_layer_surface_update_geometry(SparrowLayerSurface *layer_surf)
{
    if (!layer_surf || !layer_surf->layer_surface)
    {
        return;
    }

    struct wlr_layer_surface_v1 *wlr_ls = layer_surf->layer_surface;
    struct wlr_surface *surface = wlr_ls->surface;

    if (!wlr_ls->output)
    {
        Output *out = sparrow_get_first_output();
        if (out && out->wlr_output)
        {
            wlr_ls->output = out->wlr_output;
            layer_surf->current_output = out;
        } else
        {
            return;
        }
    } else if (!layer_surf->current_output)
    {
        Output *iter_out    = nullptr;
        const auto instance = Core::instance();
        if (instance)
        {
            wl_list_for_each(iter_out, &instance->outputs, link)
            {
                if (iter_out->wlr_output == wlr_ls->output)
                {
                    layer_surf->current_output = iter_out;
                    break;
                }
            }
        }
    }

    int out_w = 0;
    int out_h = 0;
    wlr_output_effective_resolution(wlr_ls->output, &out_w, &out_h);
    if ((out_w <= 0) || (out_h <= 0))
    {
        out_w = wlr_ls->output->width;
        out_h = wlr_ls->output->height;
    }

    const uint32_t layer =
        wlr_ls->current.layer ? wlr_ls->current.layer : wlr_ls->pending.layer;
    if ((layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) ||
        (layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM))
    {
        layer_surf->x     = 0;
        layer_surf->y     = 0;
        layer_surf->width = out_w;
        layer_surf->height = out_h;
        return;
    }

    int eff_w = 0;
    int eff_h = 0;

    if (surface && (surface->current.width > 0))
    {
        eff_w = surface->current.width;
    } else if (layer_surf->width > 0)
    {
        eff_w = layer_surf->width;
    } else if (wlr_ls->current.desired_width > 0)
    {
        eff_w = static_cast<int>(wlr_ls->current.desired_width);
    } else if (wlr_ls->pending.desired_width > 0)
    {
        eff_w = static_cast<int>(wlr_ls->pending.desired_width);
    }

    if (surface && (surface->current.height > 0))
    {
        eff_h = surface->current.height;
    } else if (layer_surf->height > 0)
    {
        eff_h = layer_surf->height;
    } else if (wlr_ls->current.desired_height > 0)
    {
        eff_h = static_cast<int>(wlr_ls->current.desired_height);
    } else if (wlr_ls->pending.desired_height > 0)
    {
        eff_h = static_cast<int>(wlr_ls->pending.desired_height);
    }

    const uint32_t anchor =
        wlr_ls->current.anchor ? wlr_ls->current.anchor : wlr_ls->pending.anchor;
    const bool anchor_left   = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) != 0;
    const bool anchor_right  = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) != 0;
    const bool anchor_top    = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) != 0;
    const bool anchor_bottom = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) != 0;

    const auto & margin = (wlr_ls->current.anchor != 0) ?
        wlr_ls->current.margin : wlr_ls->pending.margin;

    int surf_x = 0;
    int surf_y = 0;

    if (anchor_left && anchor_right)
    {
        surf_x = static_cast<int>(margin.left);
        eff_w  = std::max(0, out_w - static_cast<int>(margin.left + margin.right));
    } else if (anchor_left)
    {
        surf_x = static_cast<int>(margin.left);
    } else if (anchor_right)
    {
        surf_x = out_w - eff_w - static_cast<int>(margin.right);
    } else
    {
        surf_x = (out_w - eff_w) / 2;
    }

    if (anchor_top && anchor_bottom)
    {
        surf_y = static_cast<int>(margin.top);
        eff_h  = std::max(0, out_h - static_cast<int>(margin.top + margin.bottom));
    } else if (anchor_top)
    {
        surf_y = static_cast<int>(margin.top);
    } else if (anchor_bottom)
    {
        surf_y = out_h - eff_h - static_cast<int>(margin.bottom);
    } else
    {
        surf_y = (out_h - eff_h) / 2;
    }

    layer_surf->x     = surf_x;
    layer_surf->y     = surf_y;
    layer_surf->width = eff_w;
    layer_surf->height = eff_h;

    if ((eff_w > 0) && (eff_h > 0) && wlr_ls->output)
    {
        struct wlr_output *wlr_out = wlr_ls->output;
        int trans_w = 0, trans_h = 0;
        wlr_output_transformed_resolution(wlr_out, &trans_w, &trans_h);
        if ((trans_w <= 0) || (trans_h <= 0))
        {
            trans_w = wlr_out->width;
            trans_h = wlr_out->height;
        }

        const double scale = (wlr_out->scale > 0.0f) ? wlr_out->scale : 1.0;
        const int bx1 =
            std::max(0, static_cast<int>(std::round(static_cast<double>(surf_x) * scale)));
        const int by1 =
            std::max(0, static_cast<int>(std::round(static_cast<double>(surf_y) * scale)));
        const int bx2 =
            std::min(trans_w, static_cast<int>(std::round(static_cast<double>(surf_x + eff_w) * scale)));
        const int by2 =
            std::min(trans_h, static_cast<int>(std::round(static_cast<double>(surf_y + eff_h) * scale)));

        if ((bx2 > bx1) && (by2 > by1))
        {
            struct wlr_box local_box = {
                .x     = bx1,
                .y     = by1,
                .width = bx2 - bx1,
                .height = by2 - by1,
            };

            if (wlr_out->transform != WL_OUTPUT_TRANSFORM_NORMAL)
            {
                wlr_box_transform(&local_box, &local_box,
                    wlr_output_transform_invert(wlr_out->transform), trans_w, trans_h);
            }

            layer_surf->last_buffer_box = local_box;
        }
    }
}

static void focus_layer_surface_keyboard(struct wlr_surface *surface)
{
    const auto instance = Core::instance();
    if (!instance || !instance->seat || !surface)
    {
        return;
    }

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(instance->seat);
    if (kb != nullptr)
    {
        wlr_seat_keyboard_notify_enter(
            instance->seat, surface, kb->keycodes, kb->num_keycodes, &kb->modifiers);
    } else
    {
        wlr_seat_keyboard_notify_enter(instance->seat, surface, nullptr, 0, nullptr);
    }

    SparrowView *v = nullptr;
    wl_list_for_each(v, &instance->views_list, link)
    {
        if (v->activated)
        {
            v->activated = false;
            if (v->toplevel)
            {
                wlr_xdg_toplevel_set_activated(v->toplevel, false);
            }

            if (v->foreign_toplevel)
            {
                wlr_foreign_toplevel_handle_v1_set_activated(v->foreign_toplevel, false);
            }
        }
    }
}

static bool has_higher_focus_target(
    const SparrowView *excluded_view,
    const SparrowLayerSurface *excluded_layer_surf)
{
    const auto instance = Core::instance();
    if (!instance)
    {
        return false;
    }

    SparrowView *v = nullptr;
    wl_list_for_each(v, &instance->views_list, link)
    {
        if ((v != excluded_view) && v->xdg_surface && v->xdg_surface->surface &&
            v->xdg_surface->surface->mapped)
        {
            return true;
        }
    }

    SparrowLayerSurface *ls = nullptr;
    wl_list_for_each(ls, &instance->layer_surfaces_list, link)
    {
        if ((ls != excluded_layer_surf) && ls->layer_surface &&
            ls->layer_surface->surface && ls->layer_surface->surface->mapped &&
            (ls->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) &&
            (ls->layer_surface->current.keyboard_interactive !=
             ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE))
        {
            return true;
        }
    }

    return false;
}

static bool can_auto_focus_layer_surface(const SparrowLayerSurface *layer_surf)
{
    if (!layer_surf || !layer_surf->layer_surface)
    {
        return false;
    }

    const uint32_t layer = layer_surf->layer_surface->current.layer;
    if (layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP)
    {
        return true;
    }

    if (layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM)
    {
        return !has_higher_focus_target(nullptr, layer_surf);
    }

    return false;
}

bool sparrow_focus_fallback_bottom_layer_surface(
    const SparrowView *excluded_view,
    const SparrowLayerSurface *excluded_layer_surf)
{
    const auto instance = Core::instance();
    if (!instance || !instance->seat ||
        has_higher_focus_target(excluded_view, excluded_layer_surf))
    {
        return false;
    }

    SparrowLayerSurface *ls = nullptr;
    wl_list_for_each(ls, &instance->layer_surfaces_list, link)
    {
        if ((ls != excluded_layer_surf) && ls->layer_surface &&
            ls->layer_surface->surface && ls->layer_surface->surface->mapped &&
            (ls->layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) &&
            (ls->layer_surface->current.keyboard_interactive !=
             ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE))
        {
            focus_layer_surface_keyboard(ls->layer_surface->surface);
            return true;
        }
    }

    return false;
}

static void restore_focus_after_layer_surface_removal(
    const SparrowLayerSurface *removed_layer_surf)
{
    const auto instance = Core::instance();
    if (!instance || !instance->seat || !removed_layer_surf ||
        !removed_layer_surf->layer_surface ||
        (instance->seat->keyboard_state.focused_surface !=
         removed_layer_surf->layer_surface->surface))
    {
        return;
    }

    wlr_seat_keyboard_clear_focus(instance->seat);

    SparrowView *fallback_view = nullptr;
    SparrowView *v = nullptr;
    wl_list_for_each(v, &instance->views_list, link)
    {
        if (v->xdg_surface && v->xdg_surface->surface &&
            v->xdg_surface->surface->mapped)
        {
            if (v->activated)
            {
                fallback_view = v;
                break;
            }

            if (!fallback_view)
            {
                fallback_view = v;
            }
        }
    }

    if (fallback_view)
    {
        sparrow_view_focus(fallback_view);
        return;
    }

    sparrow_focus_fallback_bottom_layer_surface(nullptr, removed_layer_surf);
}

static void layer_surface_handle_commit(struct wl_listener *listener, void *data)
{
    SparrowLayerSurface *layer_surf = wl_container_of(listener, layer_surf, commit);
    const auto instance = Core::instance();
    if (!layer_surf || !layer_surf->layer_surface || !layer_surf->layer_surface->surface)
    {
        return;
    }

    struct wlr_layer_surface_v1 *wlr_ls = layer_surf->layer_surface;
    struct wlr_surface *surface = wlr_ls->surface;

    if (!wlr_ls->initialized)
    {
        return;
    }

    const uint32_t committed = wlr_ls->current.committed;
    const bool state_changed = (committed != 0);

    if (wlr_ls->initial_commit || !wlr_ls->configured || state_changed)
    {
        layer_surface_configure(layer_surf);
    }

    if (wlr_surface_has_buffer(surface))
    {
        sparrow_udmabuf_get_or_import_texture(surface);

        if (!layer_surf->texture_registered && surface->mapped && instance->engine)
        {
            FlutterEngineResult result = instance->embedder_api.RegisterExternalTexture(
                instance->engine, layer_surf->texture_id);
            if (result == kSuccess)
            {
                layer_surf->texture_registered = true;
                wlr_log(WLR_INFO, "Registered layer-surface texture: %ld", (long)layer_surf->texture_id);
            }
        }

        if (layer_surf->texture_registered)
        {
            instance->embedder_api.MarkExternalTextureFrameAvailable(
                instance->engine, layer_surf->texture_id);
        }

        const int cur_w = surface->current.width;
        const int cur_h = surface->current.height;
        const int old_w = layer_surf->width;
        const int old_h = layer_surf->height;
        const int old_x = layer_surf->x;
        const int old_y = layer_surf->y;

        if (layer_surf->is_sensitive && ((old_w > 0) && (old_h > 0)) &&
            ((cur_w < old_w) || (cur_h < old_h)))
        {
            instance->record_unmapped_sensitive_layer_surface(layer_surf);
        }

        layer_surf->width  = cur_w;
        layer_surf->height = cur_h;
        sparrow_layer_surface_update_geometry(layer_surf);

        if (wlr_ls->initial_commit || (old_w != layer_surf->width) ||
            (old_h != layer_surf->height) || (old_x != layer_surf->x) ||
            (old_y != layer_surf->y))
        {
            send_layer_surface_map(layer_surf);
        }
    }

    if (surface && surface->mapped &&
        can_auto_focus_layer_surface(layer_surf) &&
        (wlr_ls->current.keyboard_interactive ==
         ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) &&
        instance->seat &&
        (instance->seat->keyboard_state.focused_surface != surface))
    {
        focus_layer_surface_keyboard(surface);
    }

    if (!sparrow_layer_surface_is_occluded(layer_surf, layer_surf->current_output))
    {
        sparrow_damage_add_box(nullptr, false);
    }
}

static void layer_surface_handle_map(struct wl_listener *listener, void *data)
{
    SparrowLayerSurface *layer_surf = wl_container_of(listener, layer_surf, map);
    const auto instance = Core::instance();
    if (!layer_surf || !layer_surf->layer_surface)
    {
        return;
    }

    if (!layer_surf->texture_registered)
    {
        FlutterEngineResult result = instance->embedder_api.RegisterExternalTexture(
            instance->engine, layer_surf->texture_id);
        if (result == kSuccess)
        {
            layer_surf->texture_registered = true;
            wlr_log(WLR_INFO, "Registered layer-surface texture: %ld", (long)layer_surf->texture_id);
        } else
        {
            wlr_log(WLR_ERROR, "Failed to register layer-surface texture %ld", (long)layer_surf->texture_id);
        }
    }

    struct wlr_surface *surface = layer_surf->layer_surface->surface;
    layer_surf->width  = surface ? surface->current.width : 0;
    layer_surf->height = surface ? surface->current.height : 0;

    sparrow_layer_surface_update_geometry(layer_surf);
    sparrow_layer_surface_recompute_sensitivity(layer_surf);

    send_layer_surface_map(layer_surf);

    if (surface && wlr_surface_has_buffer(surface) && layer_surf->texture_registered)
    {
        instance->embedder_api.MarkExternalTextureFrameAvailable(
            instance->engine, layer_surf->texture_id);
    }

    if (surface)
    {
        struct wlr_subsurface *child_sub;
        wl_list_for_each(child_sub, &surface->current.subsurfaces_below,
            current.link)
        {
            sparrow_subsurface_create_layer(layer_surf, child_sub);
        }
        wl_list_for_each(child_sub, &surface->current.subsurfaces_above,
            current.link)
        {
            sparrow_subsurface_create_layer(layer_surf, child_sub);
        }

        SparrowSubSurface *sub;
        wl_list_for_each(sub, &layer_surf->subsurfaces, link)
        {
            if (sub->surface && sub->surface->mapped)
            {
                sparrow_sub_surface_get_root_coords(sub, &sub->x, &sub->y);
                sparrow_subsurface_send_position(sub);
            }
        }
    }

    if (surface &&
        can_auto_focus_layer_surface(layer_surf) &&
        (layer_surf->layer_surface->current.keyboard_interactive !=
         ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) &&
        instance->seat)
    {
        focus_layer_surface_keyboard(surface);
    }

    sparrow_damage_add_box(nullptr, false);
}

static void layer_surface_handle_unmap(struct wl_listener *listener, void *data)
{
    SparrowLayerSurface *layer_surf = wl_container_of(listener, layer_surf, unmap);
    const auto instance = Core::instance();
    if (!layer_surf || !instance)
    {
        return;
    }

    if (layer_surf->is_sensitive)
    {
        instance->record_unmapped_sensitive_layer_surface(layer_surf);
    }

    wlr_log(WLR_INFO, "LayerSurface unmapped: handle=%d", layer_surf->handle);

    restore_focus_after_layer_surface_removal(layer_surf);

    if (layer_surf->texture_registered)
    {
        instance->embedder_api.UnregisterExternalTexture(
            instance->engine, layer_surf->texture_id);
        layer_surf->texture_registered = false;
        wlr_log(WLR_INFO, "Unregistered layer-surface texture: %ld", (long)layer_surf->texture_id);
    }

    layer_surf->width  = 0;
    layer_surf->height = 0;

    send_layer_surface_unmap(layer_surf->handle);
    sparrow_damage_add_box(nullptr, false);
}

static void layer_surface_handle_destroy(struct wl_listener *listener, void *data)
{
    SparrowLayerSurface *layer_surf = wl_container_of(listener, layer_surf, destroy);
    const auto instance = Core::instance();
    if (!layer_surf || !instance)
    {
        return;
    }

    if (layer_surf->is_sensitive)
    {
        instance->record_unmapped_sensitive_layer_surface(layer_surf);
    }

    wlr_log(WLR_INFO, "LayerSurface destroyed: handle=%d", layer_surf->handle);

    restore_focus_after_layer_surface_removal(layer_surf);

    if (layer_surf->texture_registered)
    {
        instance->embedder_api.UnregisterExternalTexture(
            instance->engine, layer_surf->texture_id);
        layer_surf->texture_registered = false;
    }

    send_layer_surface_unmap(layer_surf->handle);

    SparrowSubSurface *sub, *sub_tmp;
    wl_list_for_each_safe(sub, sub_tmp, &layer_surf->subsurfaces, link)
    {
        wl_list_remove(&sub->link);
        wl_list_init(&sub->link);
        sub->parent_layer_surface = nullptr;
    }

    wl_list_remove(&layer_surf->map.link);
    wl_list_remove(&layer_surf->unmap.link);
    wl_list_remove(&layer_surf->commit.link);
    wl_list_remove(&layer_surf->destroy.link);
    if (layer_surf->new_subsurface.link.next &&
        layer_surf->new_subsurface.link.prev)
    {
        wl_list_remove(&layer_surf->new_subsurface.link);
        wl_list_init(&layer_surf->new_subsurface.link);
    }

    wl_list_remove(&layer_surf->link);

    if (instance->layer_surfaces)
    {
        handle_map_remove(instance->layer_surfaces, layer_surf->handle);
    }

    delete layer_surf;
    sparrow_damage_add_box(nullptr, false);
}

static void layer_surface_handle_new_surface(struct wl_listener *listener, void *data)
{
    auto *wlr_ls = static_cast<struct wlr_layer_surface_v1*>(data);
    const auto instance = Core::instance();
    if (!wlr_ls || !instance)
    {
        return;
    }

    wlr_log(WLR_INFO, "New layer-surface: namespace='%s', layer=%d, anchor=%d",
        wlr_ls->namespace_t ? wlr_ls->namespace_t : "unnamed",
        (int)wlr_ls->pending.layer, wlr_ls->pending.anchor);

    // If client did not specify an output, assign the primary output
    if (!wlr_ls->output)
    {
        Output *out = sparrow_get_first_output();
        if (out && out->wlr_output)
        {
            wlr_ls->output = out->wlr_output;
        } else
        {
            wlr_log(WLR_ERROR, "No output available for layer-surface");
            wlr_layer_surface_v1_destroy(wlr_ls);
            return;
        }
    }

    Output *sparrow_out = nullptr;
    Output *iter_out    = nullptr;
    wl_list_for_each(iter_out, &instance->outputs, link)
    {
        if (iter_out->wlr_output == wlr_ls->output)
        {
            sparrow_out = iter_out;
            break;
        }
    }

    auto *layer_surf = new SparrowLayerSurface();
    layer_surf->layer_surface  = wlr_ls;
    layer_surf->current_output = sparrow_out;
    wlr_ls->data = layer_surf;

    layer_surf->handle     = handle_map_add(instance->layer_surfaces, layer_surf);
    layer_surf->texture_id = layer_surf->handle + 300000;
    wl_list_init(&layer_surf->subsurfaces);

    wl_list_insert(&instance->layer_surfaces_list, &layer_surf->link);

    layer_surf->map.notify = layer_surface_handle_map;
    wl_signal_add(&wlr_ls->surface->events.map, &layer_surf->map);

    layer_surf->unmap.notify = layer_surface_handle_unmap;
    wl_signal_add(&wlr_ls->surface->events.unmap, &layer_surf->unmap);

    layer_surf->commit.notify = layer_surface_handle_commit;
    wl_signal_add(&wlr_ls->surface->events.commit, &layer_surf->commit);

    layer_surf->destroy.notify = layer_surface_handle_destroy;
    wl_signal_add(&wlr_ls->events.destroy, &layer_surf->destroy);

    layer_surf->new_subsurface.notify = sparrow_subsurface_handle_new_layer;
    wl_signal_add(&wlr_ls->surface->events.new_subsurface,
        &layer_surf->new_subsurface);

    struct wlr_subsurface *child_sub;
    wl_list_for_each(child_sub, &wlr_ls->surface->current.subsurfaces_below,
        current.link)
    {
        sparrow_subsurface_create_layer(layer_surf, child_sub);
    }
    wl_list_for_each(child_sub, &wlr_ls->surface->current.subsurfaces_above,
        current.link)
    {
        sparrow_subsurface_create_layer(layer_surf, child_sub);
    }
}

void sparrow_layer_shell_init(struct wl_display *display)
{
    const auto instance = Core::instance();
    if (!instance || !display)
    {
        return;
    }

    instance->layer_shell = wlr_layer_shell_v1_create(display, 4);
    if (!instance->layer_shell)
    {
        wlr_log(WLR_ERROR, "Failed to create wlr_layer_shell_v1");
        return;
    }

    instance->layer_shell_new_surface.notify = layer_surface_handle_new_surface;
    wl_signal_add(&instance->layer_shell->events.new_surface,
        &instance->layer_shell_new_surface);

    wlr_log(WLR_INFO, "Initialized wlr_layer_shell_v1 (version 4)");
}

SparrowLayerSurface *sparrow_find_layer_surface_by_handle(uint32_t handle)
{
    const auto instance = Core::instance();
    if (!instance || !instance->layer_surfaces)
    {
        return nullptr;
    }

    SparrowLayerSurface *layer_surf = nullptr;
    if (handle_map_get(instance->layer_surfaces, handle,
        reinterpret_cast<void**>(&layer_surf)))
    {
        return layer_surf;
    }

    return nullptr;
}

void sparrow_layer_surfaces_update_on_output_change(Output *output)
{
    const auto instance = Core::instance();
    if (!instance)
    {
        return;
    }

    SparrowLayerSurface *layer_surf = nullptr;
    wl_list_for_each(layer_surf, &instance->layer_surfaces_list, link)
    {
        if (!layer_surf || !layer_surf->layer_surface)
        {
            continue;
        }

        if (output && (layer_surf->current_output != output) &&
            (layer_surf->layer_surface->output != output->wlr_output))
        {
            continue;
        }

        layer_surface_configure(layer_surf);
        sparrow_layer_surface_update_geometry(layer_surf);
        send_layer_surface_map(layer_surf);
    }
}

void sparrow_layer_surface_recompute_sensitivity(SparrowLayerSurface *layer_surf)
{
    if (!layer_surf || !layer_surf->layer_surface)
    {
        return;
    }

    const auto instance = Core::instance();
    if (!instance)
    {
        return;
    }

    bool new_sensitive = false;
    struct wlr_layer_surface_v1 *wlr_ls = layer_surf->layer_surface;
    if (wlr_ls->surface && sparrow_surface_has_secure_output(wlr_ls->surface))
    {
        new_sensitive = true;
    }

    if (!new_sensitive && wlr_ls->namespace_t &&
        instance->is_app_sensitive(wlr_ls->namespace_t, false))
    {
        new_sensitive = true;
    }

    if (!new_sensitive && wlr_ls->surface && wlr_ls->surface->resource)
    {
        struct wl_client *client = wl_resource_get_client(wlr_ls->surface->resource);
        if (client)
        {
            pid_t pid = 0;
            uid_t uid = 0;
            gid_t gid = 0;
            wl_client_get_credentials(client, &pid, &uid, &gid);
            char proc_path[64];
            snprintf(proc_path, sizeof(proc_path), "/proc/%d/cmdline", (int)pid);
            FILE *f = fopen(proc_path, "r");
            if (f)
            {
                char cmd[256] = {};
                size_t n = fread(cmd, 1, sizeof(cmd) - 1, f);
                fclose(f);
                if (n > 0)
                {
                    cmd[n] = '\0';
                    const char *base = strrchr(cmd, '/');
                    const char *app  = base ? base + 1 : cmd;
                    if (instance->is_app_sensitive(app, false))
                    {
                        new_sensitive = true;
                    }
                }
            }
        }
    }

    if (layer_surf->is_sensitive != new_sensitive)
    {
        layer_surf->is_sensitive = new_sensitive;
        wlr_log(WLR_INFO, "[SENSITIVE] LayerSurface %u (namespace='%s') sensitivity -> %s",
            layer_surf->handle, wlr_ls->namespace_t ? wlr_ls->namespace_t : "",
            new_sensitive ? "true" : "false");
        sparrow_damage_add_box(nullptr);
    }
}

void sparrow_recompute_all_layer_surfaces_sensitivity()
{
    const auto instance = Core::instance();
    if (!instance)
    {
        return;
    }

    SparrowLayerSurface *layer_surf = nullptr;
    wl_list_for_each(layer_surf, &instance->layer_surfaces_list, link)
    {
        sparrow_layer_surface_recompute_sensitivity(layer_surf);
    }
}

bool sparrow_get_layer_surface_buffer_box(SparrowLayerSurface *layer_surf,
    struct wlr_output *wlr_out, struct wlr_box *out_buf_box, bool check_mapped)
{
    if (!layer_surf || !wlr_out || !out_buf_box)
    {
        return false;
    }

    struct wlr_layer_surface_v1 *wlr_ls = layer_surf->layer_surface;
    if (!wlr_ls || !wlr_ls->surface)
    {
        return false;
    }

    if (check_mapped && !wlr_ls->surface->mapped)
    {
        return false;
    }

    if (wlr_ls->output && (wlr_ls->output != wlr_out))
    {
        return false;
    }

    sparrow_layer_surface_update_geometry(layer_surf);

    int trans_w = 0, trans_h = 0;
    wlr_output_transformed_resolution(wlr_out, &trans_w, &trans_h);
    if ((trans_w <= 0) || (trans_h <= 0))
    {
        trans_w = wlr_out->width;
        trans_h = wlr_out->height;
    }

    const double scale = (wlr_out->scale > 0.0f) ? wlr_out->scale : 1.0;
    const double rel_x = static_cast<double>(layer_surf->x);
    const double rel_y = static_cast<double>(layer_surf->y);
    const double w     = (layer_surf->width > 0) ? static_cast<double>(layer_surf->width) :
        static_cast<double>(trans_w);
    const double h = (layer_surf->height > 0) ? static_cast<double>(layer_surf->height) :
        static_cast<double>(trans_h);

    const int bx1 = std::max(0, static_cast<int>(std::round(rel_x * scale)));
    const int by1 = std::max(0, static_cast<int>(std::round(rel_y * scale)));
    const int bx2 = std::min(trans_w, static_cast<int>(std::round((rel_x + w) * scale)));
    const int by2 = std::min(trans_h, static_cast<int>(std::round((rel_y + h) * scale)));

    if ((bx2 <= 0) || (by2 <= 0) || (bx1 >= trans_w) || (by1 >= trans_h) ||
        (bx2 <= bx1) || (by2 <= by1))
    {
        return false;
    }

    struct wlr_box local_box = {
        .x     = bx1,
        .y     = by1,
        .width = bx2 - bx1,
        .height = by2 - by1,
    };

    if (wlr_out->transform != WL_OUTPUT_TRANSFORM_NORMAL)
    {
        wlr_box_transform(&local_box, &local_box,
            wlr_output_transform_invert(wlr_out->transform), trans_w, trans_h);
    }

    if ((local_box.x + local_box.width <= 0) ||
        (local_box.y + local_box.height <= 0) ||
        (local_box.x >= wlr_out->width) || (local_box.y >= wlr_out->height) ||
        (local_box.width <= 0) || (local_box.height <= 0))
    {
        return false;
    }

    *out_buf_box = local_box;
    layer_surf->last_buffer_box = local_box;
    return true;
}

bool sparrow_get_sensitive_layer_surface_buffer_box(SparrowLayerSurface *layer_surf,
    struct wlr_output *wlr_out, struct wlr_box *out_buf_box, bool check_mapped)
{
    if (!layer_surf || !layer_surf->is_sensitive)
    {
        return false;
    }

    if (!sparrow_get_layer_surface_buffer_box(layer_surf, wlr_out, out_buf_box, check_mapped))
    {
        return false;
    }

    const int bx1 = std::max(0, out_buf_box->x - 1);
    const int by1 = std::max(0, out_buf_box->y - 1);
    const int bx2 = std::min(wlr_out->width, out_buf_box->x + out_buf_box->width + 1);
    const int by2 = std::min(wlr_out->height, out_buf_box->y + out_buf_box->height + 1);

    out_buf_box->x     = bx1;
    out_buf_box->y     = by1;
    out_buf_box->width = bx2 - bx1;
    out_buf_box->height = by2 - by1;
    layer_surf->last_buffer_box = *out_buf_box;

    return true;
}

SparrowLayerSurface *sparrow_find_layer_surface_by_surface(const struct wlr_surface *surface)
{
    if (!surface)
    {
        return nullptr;
    }

    const auto instance = Core::instance();
    if (!instance)
    {
        return nullptr;
    }

    SparrowLayerSurface *layer_surf = nullptr;
    wl_list_for_each(layer_surf, &instance->layer_surfaces_list, link)
    {
        if (layer_surf->layer_surface && (layer_surf->layer_surface->surface == surface))
        {
            return layer_surf;
        }
    }
    return nullptr;
}

bool sparrow_has_exclusive_layer_surface()
{
    const auto instance = Core::instance();
    if (!instance)
    {
        return false;
    }

    SparrowLayerSurface *layer_surf = nullptr;
    wl_list_for_each(layer_surf, &instance->layer_surfaces_list, link)
    {
        if (layer_surf->layer_surface && layer_surf->layer_surface->surface &&
            layer_surf->layer_surface->surface->mapped &&
            (layer_surf->layer_surface->current.layer >=
             ZWLR_LAYER_SHELL_V1_LAYER_TOP) &&
            (layer_surf->layer_surface->current.keyboard_interactive ==
             ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE))
        {
            return true;
        }
    }
    return false;
}

static bool has_fullscreen_top_layer(bool want_opaque)
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return false;
    }

    constexpr uint32_t all_anchors =
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;

    SparrowLayerSurface *topmost = nullptr;
    uint32_t best_layer = 0;

    SparrowLayerSurface *layer_surf = nullptr;
    wl_list_for_each(layer_surf, &instance->layer_surfaces_list, link)
    {
        if (!layer_surf->layer_surface || !layer_surf->layer_surface->surface ||
            !layer_surf->layer_surface->surface->mapped)
        {
            continue;
        }

        const auto & state = layer_surf->layer_surface->current;
        if ((state.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP) ||
            ((state.anchor & all_anchors) != all_anchors))
        {
            continue;
        }

        Output *out = layer_surf->current_output ?
            layer_surf->current_output : sparrow_get_first_output();
        if (!out || !out->wlr_output)
        {
            continue;
        }

        if (layer_surf->layer_surface->output &&
            (layer_surf->layer_surface->output != out->wlr_output))
        {
            continue;
        }

        struct wlr_box ls_box = {};
        if (!sparrow_get_layer_surface_buffer_box(layer_surf, out->wlr_output, &ls_box))
        {
            continue;
        }

        if ((ls_box.x <= 0) && (ls_box.y <= 0) &&
            (ls_box.width >= out->wlr_output->width) &&
            (ls_box.height >= out->wlr_output->height))
        {
            if (!topmost || (state.layer > best_layer))
            {
                topmost    = layer_surf;
                best_layer = state.layer;
            }
        }
    }

    if (!topmost)
    {
        return false;
    }

    const bool is_opaque =
        sparrow_surface_is_fully_opaque(topmost->layer_surface->surface);
    return is_opaque == want_opaque;
}

bool sparrow_has_fullscreen_opaque_top_layer()
{
    return has_fullscreen_top_layer(true);
}

bool sparrow_has_fullscreen_translucent_top_layer()
{
    return has_fullscreen_top_layer(false);
}

bool sparrow_layer_surface_is_occluded(const SparrowLayerSurface *layer_surf, const Output *output)
{
    if (!layer_surf || !layer_surf->layer_surface)
    {
        return false;
    }

    const Output *out = output ? output :
        (layer_surf->current_output ? layer_surf->current_output : sparrow_get_first_output());
    if (!out)
    {
        return false;
    }

    const uint32_t layer = layer_surf->layer_surface->current.layer;

    // Any layer surface under an opaque full-screen OVERLAY layer is occluded
    if (sparrow_output_has_fullscreen_opaque_overlay(out) &&
        (layer < ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY))
    {
        return true;
    }

    // Background and Bottom layer surfaces under an opaque full-screen view are occluded
    if (((layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) ||
         (layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM)) &&
        sparrow_output_has_fullscreen_opaque_view(out))
    {
        return true;
    }

    return false;
}
