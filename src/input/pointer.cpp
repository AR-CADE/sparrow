#include "pointer.hpp"
#include "flutter/platform/cursor.hpp"
#include "flutter/platform/engine/messages/seat_message.hpp"
#include "output.hpp"
#include "seat.hpp"
#include "util/handle_map.hpp"
#include <core.hpp>
#include <surface/layer_surface.hpp>
#include <surface/popup.hpp>
#include <surface/session_lock.hpp>
#include <surface/view.hpp>

void sparrow_update_direct_input_mode(Core *instance, bool enabled,
    uint32_t surface_handle, const char *source)
{
    if (!instance)
    {
        return;
    }

    if (!enabled && (instance->input.direct_mouse_button_mask != 0))
    {
        return;
    }

    if (instance->direct_input_mode != enabled)
    {
        instance->direct_input_mode    = enabled;
        instance->direct_input_surface = enabled ? surface_handle : 0;
        wlr_log(WLR_INFO, "[DIRECT-INPUT] %s (source=%s, surface=%u)",
            enabled ? "ACTIVATED" : "DEACTIVATED",
            source ? source : "unknown",
            surface_handle);

        if (instance->show_fps)
        {
            sparrow_damage_add_box(nullptr);
        }
    }
}

void sparrow_evaluate_direct_input(Core *instance, const char *source)
{
    if (!instance)
    {
        return;
    }

    struct wlr_surface *direct_surf = nullptr;
    double direct_sx = 0.0, direct_sy = 0.0;
    SparrowView *direct_view = sparrow_get_direct_input_view(
        instance, instance->cursor->x, instance->cursor->y, &direct_surf,
        &direct_sx, &direct_sy);

    if (direct_surf != nullptr)
    {
        sparrow_update_direct_input_mode(instance, true,
            direct_view ? direct_view->handle : 0,
            source ? source : "evaluate");
    } else
    {
        sparrow_update_direct_input_mode(instance, false, 0,
            source ? source : "evaluate_none");
    }
}

void sparrow_schedule_direct_input_check(Core *instance, uint32_t delay_ms)
{
    if (!instance || !instance->direct_input_settle_timer)
    {
        return;
    }

    wl_event_source_timer_update(instance->direct_input_settle_timer, (int)delay_ms);
}

SparrowView *sparrow_get_direct_input_view(Core *instance, double cursor_x,
    double cursor_y,
    struct wlr_surface **out_surface,
    double *out_sx, double *out_sy)
{
    if (sparrow_is_session_locked())
    {
        return nullptr;
    }

    if (sparrow_has_exclusive_layer_surface())
    {
        return nullptr;
    }

    if (instance->force_render_all_views || instance->gesture_active ||
        instance->screen_zoom_active)
    {
        return nullptr;
    }

    // If dock is shown (or any overlay / menu is active), direct mode must yield completely to Flutter
    if (!instance->immersive_sticky)
    {
        return nullptr;
    }

    // Yield direct mode to Flutter when an interactive popup has a grab or accepts input at cursor
    if (instance->popups && !instance->popups->empty())
    {
        for (const auto &[handle, value] : *instance->popups)
        {
            auto *popup = static_cast<SparrowPopup*>(value);
            if (popup && popup->xdg_surface && popup->xdg_surface->surface &&
                popup->xdg_surface->surface->mapped &&
                sparrow_popup_accepts_input(popup))
            {
                if (popup->xdg_popup && (popup->xdg_popup->seat != nullptr))
                {
                    return nullptr;
                }

                // Non-grabbing interactive popup: only yield if cursor falls inside its input box
                if (popup->parent_view && popup->parent_view->has_flutter_scene_box)
                {
                    const auto & pb = popup->parent_view->flutter_scene_box;
                    const double px = pb.x + popup->x + popup->input_x;
                    const double py = pb.y + popup->y + popup->input_y;
                    if ((cursor_x >= px) && (cursor_x < px + popup->input_width) &&
                        (cursor_y >= py) && (cursor_y < py + popup->input_height))
                    {
                        return nullptr;
                    }
                } else
                {
                    return nullptr;
                }
            }
        }
    }

    // If cursor is over any mapped layer surface on TOP or OVERLAY that accepts input, yield to Flutter
    SparrowLayerSurface *ls = nullptr;
    wl_list_for_each(ls, &instance->layer_surfaces_list, link)
    {
        if (ls->layer_surface && ls->layer_surface->surface &&
            ls->layer_surface->surface->mapped &&
            (ls->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP))
        {
            if ((cursor_x >= ls->x) && (cursor_x < ls->x + ls->width) &&
                (cursor_y >= ls->y) && (cursor_y < ls->y + ls->height))
            {
                const double local_x = cursor_x - ls->x;
                const double local_y = cursor_y - ls->y;
                if (wlr_surface_point_accepts_input(ls->layer_surface->surface, local_x, local_y))
                {
                    return nullptr;
                }
            }
        }
    }

    const bool is_dragging = (instance->seat != nullptr) &&
        (instance->seat->pointer_state.button_count > 0);

    // Check bottom dock area
    if (instance->output_layout != nullptr)
    {
        struct wlr_output *wlr_out = wlr_output_layout_output_at(
            instance->output_layout, cursor_x, cursor_y);
        if (wlr_out != nullptr)
        {
            struct wlr_box box;
            wlr_output_layout_get_box(instance->output_layout, wlr_out, &box);

            // If dock is hidden, the bottom 4 pixels are for the dock reveal gesture
            if (!is_dragging && (cursor_y >= box.y + box.height - 4.0))
            {
                return nullptr;
            }
        }
    }

    // Find the currently activated view
    SparrowView *active_view = nullptr;
    SparrowView *v = nullptr;
    wl_list_for_each(v, &instance->views_list, link)
    {
        if (v->activated && v->xdg_surface && v->xdg_surface->surface &&
            v->xdg_surface->surface->mapped)
        {
            active_view = v;
            break;
        }
    }

    if (active_view == nullptr)
    {
        return nullptr;
    }

    // Resolve view geometry: use flutter_scene_box if available, fallback to full output for fullscreen
    struct wlr_box scene_box = {};
    if (active_view->has_flutter_scene_box &&
        (active_view->flutter_scene_box.width > 0) &&
        (active_view->flutter_scene_box.height > 0))
    {
        scene_box = active_view->flutter_scene_box;
    } else if (active_view->fullscreen)
    {
        if (active_view->current_output && active_view->current_output->wlr_output)
        {
            scene_box.width  = active_view->current_output->wlr_output->width;
            scene_box.height = active_view->current_output->wlr_output->height;
        }
    }

    if ((scene_box.width <= 0) || (scene_box.height <= 0))
    {
        return nullptr;
    }

    double box_x = scene_box.x;
    double box_y = scene_box.y;
    if (active_view->current_output && active_view->current_output->wlr_output &&
        instance->output_layout)
    {
        struct wlr_box out_geom = {};
        wlr_output_layout_get_box(instance->output_layout,
            active_view->current_output->wlr_output, &out_geom);
        box_x += out_geom.x;
        box_y += out_geom.y;
    }

    double box_w = scene_box.width;
    double box_h = scene_box.height;

    const bool inside_box = (cursor_x >= box_x) && (cursor_x < box_x + box_w) &&
        (cursor_y >= box_y) && (cursor_y < box_y + box_h);

    if (!inside_box && !is_dragging)
    {
        return nullptr;
    }

    struct wlr_surface *parent_surface = active_view->xdg_surface->surface;
    const int vis_w =
        (active_view->width > 0) ? active_view->width : parent_surface->current.width;
    const int vis_h =
        (active_view->height > 0) ? active_view->height : parent_surface->current.height;
    if ((vis_w <= 0) || (vis_h <= 0))
    {
        return nullptr;
    }

    const double scale_x = static_cast<double>(vis_w) / box_w;
    const double scale_y = static_cast<double>(vis_h) / box_h;
    double parent_x = active_view->geo_x + (cursor_x - box_x) * scale_x;
    double parent_y = active_view->geo_y + (cursor_y - box_y) * scale_y;

    double sx = parent_x;
    double sy = parent_y;
    struct wlr_surface *surf = wlr_surface_surface_at(parent_surface, parent_x,
                                                      parent_y, &sx, &sy);
    if ((surf == nullptr) && is_dragging)
    {
        surf = parent_surface;
        sx   = parent_x;
        sy   = parent_y;
    }

    if (surf == nullptr)
    {
        return nullptr;
    }

    if (out_surface)
    {
        *out_surface = surf;
    }

    if (out_sx)
    {
        *out_sx = sx;
    }

    if (out_sy)
    {
        *out_sy = sy;
    }

    return active_view;
}

void process_cursor_motion(uint32_t time, double dx, double dy,
    double dx_unaccel, double dy_unaccel,
    bool is_trackpad)
{
    const auto instance = Core::instance();

#ifdef DEBUG
    if (instance->debug_pointer)
    {
        wlr_log(WLR_INFO,
            "[POINTER] process_cursor_motion: time=%u, delta=(%.2f, %.2f), unaccel=(%.2f, %.2f), trackpad=%d, cursor=(%.2f, %.2f), btn_mask=0x%x",
            time, dx, dy, dx_unaccel, dy_unaccel, (int)is_trackpad,
            instance->cursor->x, instance->cursor->y,
            instance->input.fl_mouse_button_mask);
    }

#endif

    // Session Lock mode - route cursor strictly to lock surface
    if (sparrow_is_session_locked())
    {
        double sx = 0, sy = 0;
        struct wlr_surface *surface = sparrow_session_lock_surface_at(
            instance->cursor->x, instance->cursor->y, &sx, &sy);
        if (surface != nullptr)
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, sx, sy);
            wlr_seat_pointer_notify_motion(instance->seat, time, sx, sy);
            wlr_seat_pointer_notify_frame(instance->seat);
            if (instance->seat->keyboard_state.focused_surface != surface)
            {
                sparrow_session_lock_focus_surface(surface);
            }
        } else
        {
            wlr_seat_pointer_clear_focus(instance->seat);
        }

        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);

        static double s_prev_cx = -1.0, s_prev_cy = -1.0;
        if ((s_prev_cx >= 0.0) && (s_prev_cy >= 0.0))
        {
            struct wlr_box old_cursor_box = {.x = (int)s_prev_cx - 32,
                .y     = (int)s_prev_cy - 32,
                .width = 80,
                .height = 80};
            sparrow_damage_add_box(&old_cursor_box, false);
        }

        int new_cx = (int)instance->cursor->x;
        int new_cy = (int)instance->cursor->y;
        struct wlr_box new_cursor_box = {
            .x = new_cx - 32, .y = new_cy - 32, .width = 80, .height = 80};
        sparrow_damage_add_box(&new_cursor_box, false);

        s_prev_cx = instance->cursor->x;
        s_prev_cy = instance->cursor->y;
        return;
    }

    // Ensure cursor is visible if hidden by idle timeout
    if (!instance->cursor_visible)
    {
        instance->cursor_visible = true;
        if (instance->client_cursor_surface != nullptr)
        {
            wlr_cursor_set_surface(instance->cursor, instance->client_cursor_surface,
                instance->client_cursor_hotspot_x, instance->client_cursor_hotspot_y);
        } else
        {
            sparrow_cursor_reset_to_flutter();
        }
    }

    if (instance->cursor_idle_timer != nullptr)
    {
        wl_event_source_timer_update(instance->cursor_idle_timer, 3000);
    }

    // Check for bottom-edge dock reveal gesture in fullscreen mode
    if (instance->output_layout != nullptr)
    {
        struct wlr_output *wlr_out = wlr_output_layout_output_at(
            instance->output_layout, instance->cursor->x, instance->cursor->y);
        if (wlr_out != nullptr)
        {
            struct wlr_box box;
            wlr_output_layout_get_box(instance->output_layout, wlr_out, &box);

            const double bottom_threshold =
                box.y + box.height - 4.0; // bottom 4 pixels
            const double disarm_threshold =
                box.y + box.height - 12.0; // move 12px away to re-arm

            if (instance->cursor->y < disarm_threshold)
            {
                instance->pointer_bottom_gesture_armed = true;
                instance->pointer_in_bottom_edge = false;
                if (instance->pointer_bottom_dwell_timer != nullptr)
                {
                    wl_event_source_timer_update(instance->pointer_bottom_dwell_timer, 0);
                }
            }

            if (instance->pointer_bottom_gesture_armed &&
                instance->immersive_sticky)
            {
                bool has_views = !wl_list_empty(&instance->views_list) &&
                    !instance->force_render_all_views;

                if (has_views && instance->pigeon_flutter_api)
                {
                    uint64_t now_ms = seat_get_flutter_timestamp(time) / 1000;

                    if (instance->cursor->y >= bottom_threshold)
                    {
                        if (!instance->pointer_in_bottom_edge)
                        {
                            instance->pointer_in_bottom_edge = true;
                            instance->pointer_bottom_edge_enter_time_ms = now_ms;
                            instance->pointer_bottom_edge_start_y = instance->cursor->y;
                            if (instance->pointer_bottom_dwell_timer != nullptr)
                            {
                                wl_event_source_timer_update(
                                    instance->pointer_bottom_dwell_timer, 180);
                            }
                        } else if (now_ms >=
                                   instance->pointer_bottom_edge_enter_time_ms + 180)
                        {
                            wlr_log(
                                WLR_INFO,
                                "[DOCK] Pointer dwell at bottom edge (%lu ms), revealing "
                                "dock",
                                (unsigned long)(now_ms -
                                    instance->pointer_bottom_edge_enter_time_ms));
                            instance->immersive_sticky = false;
                            instance->pointer_bottom_gesture_armed = false;
                            instance->pointer_in_bottom_edge = false;
                            if (instance->pointer_bottom_dwell_timer != nullptr)
                            {
                                wl_event_source_timer_update(
                                    instance->pointer_bottom_dwell_timer, 0);
                            }

                            sparrow_pointer_constraints_deactivate(instance);
                            instance->sparrow_renderer.current_scene.needs_update = true;
                            instance->pigeon_flutter_api->DockReveal(
                                [] () {}, [] (const sparrow::FlutterError &) {});
                            sparrow_damage_add_box(nullptr);
                        }
                    } else if (instance->pointer_in_bottom_edge)
                    {
                        // Moving upward from the bottom edge (swipe-up gesture)
                        double swipe_up_dist =
                            instance->pointer_bottom_edge_start_y - instance->cursor->y;
                        if ((swipe_up_dist >= 8.0) &&
                            ((now_ms - instance->pointer_bottom_edge_enter_time_ms) <=
                             600))
                        {
                            wlr_log(WLR_INFO,
                                "[DOCK] Pointer swipe-up at bottom edge, revealing dock");
                            instance->immersive_sticky = false;
                            instance->pointer_bottom_gesture_armed = false;
                            instance->pointer_in_bottom_edge = false;
                            if (instance->pointer_bottom_dwell_timer != nullptr)
                            {
                                wl_event_source_timer_update(
                                    instance->pointer_bottom_dwell_timer, 0);
                            }

                            sparrow_pointer_constraints_deactivate(instance);
                            instance->sparrow_renderer.current_scene.needs_update = true;
                            instance->pigeon_flutter_api->DockReveal(
                                [] () {}, [] (const sparrow::FlutterError &) {});
                            sparrow_damage_add_box(nullptr);
                        } else if (swipe_up_dist < 0.0)
                        {
                            instance->pointer_bottom_edge_start_y = instance->cursor->y;
                        }
                    }
                }
            }
        }
    }

    // Direct input mode - bypass Flutter for active surface (fullscreen & edge-to-edge)
    struct wlr_surface *direct_surf = nullptr;
    double direct_sx = 0.0, direct_sy = 0.0;
    SparrowView *direct_view = sparrow_get_direct_input_view(
        instance, instance->cursor->x, instance->cursor->y,
        &direct_surf, &direct_sx, &direct_sy);

    // If a direct button is currently held down, keep motion routed to the direct view
    if ((direct_view == nullptr) && (instance->input.direct_mouse_button_mask != 0) &&
        (instance->direct_input_surface != 0))
    {
        SparrowView *v = instance->find_view_by_handle(instance->direct_input_surface);
        if (v && v->xdg_surface && v->xdg_surface->surface)
        {
            direct_view = v;
            direct_surf = v->xdg_surface->surface;
            direct_sx   = instance->cursor->x - v->x;
            direct_sy   = instance->cursor->y - v->y;
        }
    }

    if ((direct_view != nullptr) && (direct_surf != nullptr))
    {
        sparrow_update_direct_input_mode(instance, true, direct_view->handle, "pointer_motion");

        wlr_seat_pointer_notify_enter(instance->seat, direct_surf, direct_sx, direct_sy);
        wlr_seat_pointer_notify_motion(instance->seat, time, direct_sx, direct_sy);

        if ((dx != 0.0) || (dy != 0.0))
        {
            wlr_relative_pointer_manager_v1_send_relative_motion(
                instance->relative_pointer_manager, instance->seat,
                (uint64_t)time * 1000, dx, dy, dx_unaccel, dy_unaccel);
        }

        wlr_seat_pointer_notify_frame(instance->seat);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);

        // Notify Flutter so mouse idle timer is continuously reset
        FlutterPointerPhase phase =
            instance->input.fl_mouse_button_mask != 0 || is_trackpad ? kMove : kHover;
        auto timestamp = seat_get_flutter_timestamp(time);
        send_flutter_mouse_event(phase, kFlutterPointerSignalKindNone, dx, dy,
            timestamp);
        return;
    }

    sparrow_update_direct_input_mode(instance, false, 0, "pointer_motion_flutter");

    // Flutter-first: Send all motion events to Flutter
    // Flutter's widget tree does hit testing and forwards to surfaces as needed
    FlutterPointerPhase phase =
        instance->input.fl_mouse_button_mask != 0 || is_trackpad ? kMove : kHover;

    auto timestamp = seat_get_flutter_timestamp(time);

    send_flutter_mouse_event(phase, kFlutterPointerSignalKindNone, dx, dy,
        timestamp);

    // Cursor motion is handled via wlroots wlr_cursor_move:
    // - On DRM with hardware cursor plane: 0 composite damage, plane updates asynchronously
    // - On software cursor fallbacks: wlroots emits output->events.damage with exact bounds
}

struct sparrow_pointer_constraint
{
    struct wlr_pointer_constraint_v1 *wlr_constraint = nullptr;
    struct wl_listener destroy = {};
};

static void handle_constraint_destroy(struct wl_listener *listener,
    void *data)
{
    (void)data;
    struct sparrow_pointer_constraint *constraint =
        wl_container_of(listener, constraint, destroy);
    const auto instance = Core::instance();
    if (instance && (instance->active_constraint == constraint->wlr_constraint))
    {
        sparrow_pointer_constraints_deactivate(instance);
    }

    wl_list_remove(&constraint->destroy.link);
    delete constraint;
}

void sparrow_pointer_constraints_deactivate(Core *core)
{
    if (!core || !core->active_constraint)
    {
        return;
    }

    struct wlr_pointer_constraint_v1 *constraint = core->active_constraint;
    core->active_constraint = nullptr;
    wlr_pointer_constraint_v1_send_deactivated(constraint);
}

void sparrow_pointer_constraints_set_focus(Core *core,
    struct wlr_surface *surface)
{
    if (!core || !core->pointer_constraints || !core->seat)
    {
        return;
    }

    if (core->active_constraint &&
        (core->active_constraint->surface == surface))
    {
        return;
    }

    sparrow_pointer_constraints_deactivate(core);

    if (!surface)
    {
        return;
    }

    struct wlr_pointer_constraint_v1 *constraint =
        wlr_pointer_constraints_v1_constraint_for_surface(
            core->pointer_constraints, surface, core->seat);

    if (constraint)
    {
        core->active_constraint = constraint;
        wlr_pointer_constraint_v1_send_activated(constraint);
    }
}

void handle_new_pointer_constraint(struct wl_listener *listener, void *data)
{
    (void)listener;
    const auto instance = Core::instance();
    struct wlr_pointer_constraint_v1 *wlr_constraint =
        static_cast<wlr_pointer_constraint_v1*>(data);

    struct sparrow_pointer_constraint *constraint =
        new sparrow_pointer_constraint();
    constraint->wlr_constraint = wlr_constraint;
    constraint->destroy.notify = handle_constraint_destroy;
    wl_signal_add(&wlr_constraint->events.destroy, &constraint->destroy);

    if (instance && instance->seat)
    {
        struct wlr_surface *focused = instance->seat->pointer_state.focused_surface;
        if (focused && (focused == wlr_constraint->surface))
        {
            sparrow_pointer_constraints_set_focus(instance, focused);
        }
    }
}

void sparrow_pointer_constraints_init(Core *core)
{
    if (!core || !core->pointer_constraints)
    {
        return;
    }

    core->new_pointer_constraint.notify = handle_new_pointer_constraint;
    wl_signal_add(&core->pointer_constraints->events.new_constraint,
        &core->new_pointer_constraint);
}

static void sparrow_pointer_constraint_confine(
    const struct wlr_pointer_constraint_v1 *constraint)
{
    const auto instance = Core::instance();

    if (!instance || !constraint || !constraint->surface)
    {
        return;
    }

    const SparrowView *focused = seat_get_focus();
    if (!focused)
    {
        return;
    }

    double min_x = focused->x;
    double max_x = focused->x + (focused->width > 0 ? focused->width : 1) - 1.0;
    double min_y = focused->y;
    double max_y = focused->y + (focused->height > 0 ? focused->height : 1) - 1.0;

    double cx = instance->cursor->x;
    double cy = instance->cursor->y;

    double clamped_x = std::clamp(cx, min_x, max_x);
    double clamped_y = std::clamp(cy, min_y, max_y);

    if ((clamped_x != cx) || (clamped_y != cy))
    {
        wlr_cursor_warp(instance->cursor, nullptr, clamped_x, clamped_y);
    }
}

void on_server_cursor_motion(struct wl_listener *listener, void *data)
{
    (void)listener;
    const auto instance = Core::instance();
    struct wlr_pointer_motion_event *event =
        static_cast<wlr_pointer_motion_event*>(data);

    if (instance->active_constraint &&
        (instance->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED))
    {
        // When pointer is locked, directly send relative motion to the client with
        // zero latency!
        wlr_relative_pointer_manager_v1_send_relative_motion(
            instance->relative_pointer_manager, instance->seat,
            (uint64_t)event->time_msec * 1000, event->delta_x, event->delta_y,
            event->unaccel_dx, event->unaccel_dy);
        wlr_seat_pointer_notify_frame(instance->seat);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
        return;
    }

    if (!instance->cursor_visible)
    {
        instance->cursor_visible = true;
        if (instance->client_cursor_surface != nullptr)
        {
            wlr_cursor_set_surface(instance->cursor, instance->client_cursor_surface,
                instance->client_cursor_hotspot_x, instance->client_cursor_hotspot_y);
        } else
        {
            sparrow_cursor_reset_to_flutter();
        }
    }

    if (instance->cursor_idle_timer != nullptr)
    {
        wl_event_source_timer_update(instance->cursor_idle_timer, 3000);
    }

    struct sparrow_motion_cache motion = {
        .time_msec = event->time_msec,
        .valid     = true,
        .type = SPARROW_MOTION_CACHE_RELATIVE,
        .dx   = event->delta_x,
        .dy   = event->delta_y,
        .dx_unaccel = event->unaccel_dx,
        .dy_unaccel = event->unaccel_dy,
    };
    instance->input.motions.push_back(motion);

    wlr_cursor_move(instance->cursor, &event->pointer->base, event->delta_x,
        event->delta_y);

    if (instance->active_constraint && (instance->active_constraint->type ==
                                        WLR_POINTER_CONSTRAINT_V1_CONFINED))
    {
        sparrow_pointer_constraint_confine(instance->active_constraint);
    }

    process_cursor_motion(event->time_msec, event->delta_x, event->delta_y,
        event->unaccel_dx, event->unaccel_dy);
}

void on_server_cursor_motion_absolute(struct wl_listener *listener,
    void *data)
{
    (void)listener;
    const auto instance = Core::instance();
    struct wlr_pointer_motion_absolute_event *event =
        static_cast<wlr_pointer_motion_absolute_event*>(data);

    struct wlr_box total_box = {};
    wlr_output_layout_get_box(instance->output_layout, nullptr, &total_box);
    double screen_width  = total_box.width > 0 ? total_box.width : 1.0;
    double screen_height = total_box.height > 0 ? total_box.height : 1.0;

    double cur_x = event->x * screen_width;
    double cur_y = event->y * screen_height;

    static double last_abs_x = -1.0, last_abs_y = -1.0;
    double dx = (last_abs_x >= 0.0) ? cur_x - last_abs_x : 0.0;
    double dy = (last_abs_y >= 0.0) ? cur_y - last_abs_y : 0.0;
    last_abs_x = cur_x;
    last_abs_y = cur_y;

    if (instance->active_constraint &&
        (instance->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED))
    {
        // In locked mode, send actual relative delta (dx, dy) directly to client!
        wlr_relative_pointer_manager_v1_send_relative_motion(
            instance->relative_pointer_manager, instance->seat,
            (uint64_t)event->time_msec * 1000, dx, dy, dx, dy);
        wlr_seat_pointer_notify_frame(instance->seat);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
        return;
    }

    if (!instance->cursor_visible)
    {
        instance->cursor_visible = true;
        if (instance->client_cursor_surface != nullptr)
        {
            wlr_cursor_set_surface(instance->cursor, instance->client_cursor_surface,
                instance->client_cursor_hotspot_x, instance->client_cursor_hotspot_y);
        } else
        {
            sparrow_cursor_reset_to_flutter();
        }
    }

    if (instance->cursor_idle_timer != nullptr)
    {
        wl_event_source_timer_update(instance->cursor_idle_timer, 3000);
    }

    struct sparrow_motion_cache motion = {
        .time_msec = event->time_msec,
        .valid     = true,
        .type = SPARROW_MOTION_CACHE_ABSOLUTE,
        .dx   = dx,
        .dy   = dy,
        .dx_unaccel = dx,
        .dy_unaccel = dy,
    };
    instance->input.motions.push_back(motion);

    wlr_cursor_warp_absolute(instance->cursor, &event->pointer->base, event->x,
        event->y);

    if (instance->active_constraint && (instance->active_constraint->type ==
                                        WLR_POINTER_CONSTRAINT_V1_CONFINED))
    {
        sparrow_pointer_constraint_confine(instance->active_constraint);
    }

    process_cursor_motion(event->time_msec, dx, dy, dx, dy);
}

void on_server_cursor_button(struct wl_listener *listener, void *data)
{
    (void)listener;
    const auto instance = Core::instance();
    struct wlr_pointer_button_event *event =
        static_cast<wlr_pointer_button_event*>(data);

#ifdef DEBUG
    if (instance->debug_pointer)
    {
        wlr_log(WLR_INFO,
            "[POINTER] cursor_button: button=0x%x (%u) state=%d cursor=(%.2f, "
            "%.2f) direct=%d lock=%d time_msec=%u",
            event->button, event->button, event->state, instance->cursor->x,
            instance->cursor->y, instance->direct_input_mode,
            sparrow_is_session_locked(), event->time_msec);
    }

#endif

    // Session Lock mode - route button strictly to lock surface
    if (sparrow_is_session_locked())
    {
        double sx = 0, sy = 0;
        struct wlr_surface *surface = sparrow_session_lock_surface_at(
            instance->cursor->x, instance->cursor->y, &sx, &sy);
        if (surface != nullptr)
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, sx, sy);
            wlr_seat_pointer_notify_button(instance->seat, event->time_msec,
                event->button, event->state);
            wlr_seat_pointer_notify_frame(instance->seat);
            if (instance->seat->keyboard_state.focused_surface != surface)
            {
                sparrow_session_lock_focus_surface(surface);
            }
        }

        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
        return;
    }

    if (!instance->cursor_visible)
    {
        instance->cursor_visible = true;
        if (instance->client_cursor_surface != nullptr)
        {
            wlr_cursor_set_surface(instance->cursor, instance->client_cursor_surface,
                instance->client_cursor_hotspot_x, instance->client_cursor_hotspot_y);
        } else
        {
            sparrow_cursor_reset_to_flutter();
        }
    }

    if (instance->cursor_idle_timer != nullptr)
    {
        wl_event_source_timer_update(instance->cursor_idle_timer, 3000);
    }

    uint32_t flutter_button_mask =
        static_cast<uint32_t>(seat_flutter_button_mask_from_linux(event->button));

    // Pointer Lock mode - bypass Flutter for low-latency gaming / locked pointer
    if (instance->active_constraint &&
        (instance->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED))
    {
        wlr_seat_pointer_notify_button(instance->seat, event->time_msec,
            event->button, event->state);
        wlr_seat_pointer_notify_frame(instance->seat);
        return;
    }

    if (!instance->cursor_visible)
    {
        instance->cursor_visible = true;
        if (instance->client_cursor_surface != nullptr)
        {
            wlr_cursor_set_surface(instance->cursor, instance->client_cursor_surface,
                instance->client_cursor_hotspot_x, instance->client_cursor_hotspot_y);
        } else
        {
            sparrow_cursor_reset_to_flutter();
        }
    }

    // Direct input mode - bypass Flutter for active surface (fullscreen & edge-to-edge)
    struct wlr_surface *direct_surf = nullptr;
    double direct_sx = 0.0, direct_sy = 0.0;
    SparrowView *direct_view = sparrow_get_direct_input_view(
        instance, instance->cursor->x, instance->cursor->y,
        &direct_surf, &direct_sx, &direct_sy);

    if (flutter_button_mask == 0)
    {
        if (direct_view != nullptr)
        {
            wlr_seat_pointer_notify_button(instance->seat, event->time_msec,
                event->button, event->state);
            wlr_seat_pointer_notify_frame(instance->seat);
        }

        return;
    }

    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        if ((direct_view != nullptr) && (instance->input.fl_mouse_button_mask == 0))
        {
            sparrow_update_direct_input_mode(instance, true, direct_view->handle, "pointer_button");
            instance->input.direct_mouse_button_mask |= flutter_button_mask;

            wlr_seat_pointer_notify_button(instance->seat, event->time_msec,
                event->button, event->state);
            wlr_seat_pointer_notify_frame(instance->seat);
            return;
        }

        instance->input.fl_mouse_button_mask |= flutter_button_mask;
    } else
    {
        if ((instance->input.direct_mouse_button_mask & flutter_button_mask) != 0)
        {
            instance->input.direct_mouse_button_mask &= ~flutter_button_mask;
            wlr_seat_pointer_notify_button(instance->seat, event->time_msec,
                event->button, event->state);
            wlr_seat_pointer_notify_frame(instance->seat);

            if (instance->input.direct_mouse_button_mask == 0)
            {
                if (direct_view == nullptr)
                {
                    sparrow_update_direct_input_mode(instance, false, 0, "pointer_button_release_exit");
                }

                auto timestamp = seat_get_flutter_timestamp(event->time_msec);
                send_flutter_mouse_event(kHover, kFlutterPointerSignalKindNone, 0.0,
                    0.0, timestamp);
            }

            return;
        }

        instance->input.fl_mouse_button_mask &= ~flutter_button_mask;
    }

    sparrow_update_direct_input_mode(instance, false, 0, "pointer_button_flutter");

// Flutter-first: Send all button events to Flutter
// Flutter's widget tree does hit testing and forwards to surfaces as needed
//
// Cache the original wlroots timestamp so we can use it when the platform
// channel response arrives. Wayland clients expect hardware timestamps.
    instance->input.last_button = {
        .time_msec = event->time_msec,
        .valid     = true,
    };

    auto timestamp = seat_get_flutter_timestamp(event->time_msec);

    send_flutter_mouse_event(
        event->state == WL_POINTER_BUTTON_STATE_PRESSED ? kDown : kUp,
        kFlutterPointerSignalKindNone, 0.0, 0.0, timestamp);
}

void on_server_cursor_axis(struct wl_listener *listener, void *data)
{
    (void)listener;
    const auto instance = Core::instance();
    struct wlr_pointer_axis_event *event =
        static_cast<wlr_pointer_axis_event*>(data);

    // Session Lock mode - route axis strictly to lock surface
    if (sparrow_is_session_locked())
    {
        double sx = 0, sy = 0;
        struct wlr_surface *surface = sparrow_session_lock_surface_at(
            instance->cursor->x, instance->cursor->y, &sx, &sy);
        if (surface != nullptr)
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, sx, sy);
            wlr_seat_pointer_notify_axis(
                instance->seat, event->time_msec, event->orientation, event->delta,
                event->delta_discrete, event->source, event->relative_direction);
            wlr_seat_pointer_notify_frame(instance->seat);
            if (instance->seat->keyboard_state.focused_surface != surface)
            {
                sparrow_session_lock_focus_surface(surface);
            }
        }

        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
        return;
    }

    if (!instance->cursor_visible)
    {
        instance->cursor_visible = true;
        if (instance->client_cursor_surface != nullptr)
        {
            wlr_cursor_set_surface(instance->cursor, instance->client_cursor_surface,
                instance->client_cursor_hotspot_x, instance->client_cursor_hotspot_y);
        } else
        {
            sparrow_cursor_reset_to_flutter();
        }
    }

    if (instance->cursor_idle_timer != nullptr)
    {
        wl_event_source_timer_update(instance->cursor_idle_timer, 3000);
    }

    // Intercept Super + Scroll for Wayfire/KDE style desktop zoom (checked first so zoom works during gaming
    /// lock)
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(instance->seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    if ((mods & WLR_MODIFIER_LOGO) != 0)
    {
        double delta = (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ?
            event->delta :
            0.0;
        if (delta != 0.0)
        {
            send_zoom_scroll(delta, instance->cursor->x, instance->cursor->y);
        }

        return;
    }

    // Fullscreen 4-anchor opaque TOP layer surface: block PageView control and
    // forward axis events directly to the seat client.
    if (sparrow_has_fullscreen_opaque_top_layer())
    {
        wlr_seat_pointer_notify_axis(
            instance->seat, event->time_msec, event->orientation, event->delta,
            event->delta_discrete, event->source, event->relative_direction);
        wlr_seat_pointer_notify_frame(instance->seat);
        return;
    }

    const bool is_touchpad = (event->source == WL_POINTER_AXIS_SOURCE_FINGER) ||
        (event->source == WL_POINTER_AXIS_SOURCE_CONTINUOUS);

    // Pointer Lock mode - bypass Flutter for low-latency gaming / locked pointer
    if (instance->active_constraint &&
        (instance->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED))
    {
        wlr_seat_pointer_notify_axis(
            instance->seat, event->time_msec, event->orientation, event->delta,
            event->delta_discrete, event->source, event->relative_direction);
        wlr_seat_pointer_notify_frame(instance->seat);
        return;
    }

    // Direct input mode - bypass Flutter for active surface (fullscreen & edge-to-edge),
    // unless a fullscreen translucent TOP/OVERLAY layer surface (e.g. slurp) is active and
    // the user is performing a 2-finger touchpad gesture to switch pages.
    if (!(is_touchpad && sparrow_has_fullscreen_translucent_top_layer()))
    {
        struct wlr_surface *direct_surf = nullptr;
        double direct_sx = 0.0, direct_sy = 0.0;
        SparrowView *direct_view = sparrow_get_direct_input_view(
            instance, instance->cursor->x, instance->cursor->y,
            &direct_surf, &direct_sx, &direct_sy);

        if (direct_view != nullptr)
        {
            sparrow_update_direct_input_mode(instance, true, direct_view->handle, "pointer_axis");

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] direct_axis: surface=%u, source=%d (%s), orient=%d (%s), delta=%.4f, discrete=%d, rel_dir=%d, time_msec=%u",
                    direct_view->handle, event->source, is_touchpad ? "touchpad/finger" : "wheel",
                    event->orientation,
                    (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
                    event->delta, event->delta_discrete, event->relative_direction, event->time_msec);
            }

#endif

            wlr_seat_pointer_notify_axis(
                instance->seat, event->time_msec, event->orientation, event->delta,
                event->delta_discrete, event->source, event->relative_direction);
            wlr_seat_pointer_notify_frame(instance->seat);
            return;
        }
    }

    sparrow_update_direct_input_mode(instance, false, 0, "pointer_axis_flutter");

#ifdef DEBUG
    if (instance->debug_pointer)
    {
        wlr_log(WLR_INFO,
            "[POINTER] cursor_axis: source=%d (%s), orient=%d (%s), delta=%.4f, discrete=%d, rel_dir=%d, time_msec=%u",
            event->source, is_touchpad ? "touchpad/finger" : "wheel",
            event->orientation,
            (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
            event->delta, event->delta_discrete, event->relative_direction, event->time_msec);
    }

#endif

    struct sparrow_scroll_cache scroll_entry = {
        .delta = event->delta,
        .delta_discrete = event->delta_discrete,
        .source = event->source,
        .relative_direction = event->relative_direction,
        .orientation = event->orientation,
        .time_msec   = event->time_msec,
        .valid = true,
    };
    instance->input.scrolls.push_back(scroll_entry);

    double scroll_delta_x = 0.0;
    double scroll_delta_y = 0.0;
    switch (event->orientation)
    {
      case WL_POINTER_AXIS_VERTICAL_SCROLL:
        scroll_delta_y = -event->delta;
        break;

      case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
        scroll_delta_x = -event->delta;
        break;
    }

    const auto timestamp = seat_get_flutter_timestamp(event->time_msec);

    if (is_touchpad)
    {
        // Standard GTK / Chromium multiplier (53.0 / 10.0 = 5.3), consistent with
        // sparrow-app-runner.
        const double kTrackpadHorizontalMultiplier = 5.3;

        // In the compositor overview, the PageView uses viewportFraction = 0.5
        // (half-width cards) without pageSnapping, making full 5.3x acceleration
        // feel overly slippery. Scaling by kTrackpadHorizontalMultiplier / 2.0
        // (~2.65x) provides smooth, responsive finger tracking.
        scroll_delta_x *= (kTrackpadHorizontalMultiplier / 2.0);

        if (event->delta == 0.0)
        {
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] touchpad lift (delta=0): pan_started=%d, pan=(%.2f, %.2f), timestamp=%zu",
                    (int)instance->input.pan_started, instance->input.pan_x,
                    instance->input.pan_y, timestamp);
            }

#endif
            if (instance->input.pan_started)
            {
                send_flutter_trackpad_event(kPanZoomEnd, timestamp,
                    instance->input.pan_x,
                    instance->input.pan_y, 0.0, 0.0);
                instance->input.pan_started = false;
                instance->input.pan_x = 0.0;
                instance->input.pan_y = 0.0;
            }
        } else
        {
            if (!instance->input.pan_started)
            {
                instance->input.pan_x = 0.0;
                instance->input.pan_y = 0.0;
                send_flutter_trackpad_event(kPanZoomStart, timestamp, 0.0, 0.0, 0.0,
                    0.0);
                instance->input.pan_started = true;
            }

            instance->input.pan_x += scroll_delta_x;
            instance->input.pan_y += scroll_delta_y;

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] touchpad pan: pan_started=%d, delta=(%.2f, %.2f), total_pan=(%.2f, %.2f), timestamp=%zu",
                    (int)instance->input.pan_started, scroll_delta_x, scroll_delta_y,
                    instance->input.pan_x, instance->input.pan_y, timestamp);
            }

#endif

            send_flutter_trackpad_event(kPanZoomUpdate, timestamp,
                instance->input.pan_x, instance->input.pan_y,
                1.0, 0.0);
        }
    } else
    {
#ifdef DEBUG
        if (instance->debug_pointer)
        {
            wlr_log(WLR_INFO,
                "[POINTER] mouse wheel: delta=(%.2f, %.2f), timestamp=%zu",
                scroll_delta_x, scroll_delta_y, timestamp);
        }

#endif
        send_flutter_mouse_event(kMove, kFlutterPointerSignalKindScroll,
            scroll_delta_x, scroll_delta_y, timestamp);
    }
}

void on_server_cursor_frame(struct wl_listener *listener, void *data)
{
    (void)listener;
    (void)data;
    const auto instance = Core::instance();
    if (instance->direct_input_mode && (instance->seat != nullptr))
    {
        wlr_seat_pointer_notify_frame(instance->seat);
    }
}

void handle_swipe_begin(struct wl_listener *listener, void *data)
{
    (void)listener;
    if (sparrow_is_session_locked())
    {
        return;
    }

    const auto core = Core::instance();
    struct wlr_pointer_swipe_begin_event *event =
        static_cast<wlr_pointer_swipe_begin_event*>(data);

#ifdef DEBUG
    if (core->debug_pointer)
    {
        wlr_log(WLR_INFO, "[POINTER] swipe_begin: time_msec=%u, fingers=%u",
            event->time_msec, event->fingers);
    }

#endif

    if (event->fingers >= 3)
    {
        if ((event->fingers >= 3) && !sparrow_has_fullscreen_opaque_top_layer())
        {
            core->input.is_compositor_swipe = true;
            send_gesture_swipe_begin(event->fingers, event->time_msec);
        } else
        {
            core->input.is_compositor_swipe = false;
            if (core->pointer_gestures)
            {
                wlr_pointer_gestures_v1_send_swipe_begin(
                    core->pointer_gestures, core->seat, event->time_msec, event->fingers);
            }
        }
    }
}

void handle_swipe_update(struct wl_listener *listener, void *data)
{
    (void)listener;
    if (sparrow_is_session_locked())
    {
        return;
    }

    const auto core = Core::instance();
    struct wlr_pointer_swipe_update_event *event =
        static_cast<wlr_pointer_swipe_update_event*>(data);

#ifdef DEBUG
    if (core->debug_pointer)
    {
        wlr_log(WLR_INFO,
            "[POINTER] swipe_update: dx=%.2f, dy=%.2f, time_msec=%u, is_compositor=%d",
            event->dx, event->dy, event->time_msec, (int)core->input.is_compositor_swipe);
    }

#endif

    if (core->input.is_compositor_swipe)
    {
        send_gesture_swipe_update(event->dx, event->dy, event->time_msec);
    } else
    {
        if (core->pointer_gestures)
        {
            wlr_pointer_gestures_v1_send_swipe_update(core->pointer_gestures,
                core->seat, event->time_msec,
                event->dx, event->dy);
        }
    }
}

void handle_swipe_end(struct wl_listener *listener, void *data)
{
    (void)listener;
    if (sparrow_is_session_locked())
    {
        return;
    }

    const auto core = Core::instance();
    struct wlr_pointer_swipe_end_event *event =
        static_cast<wlr_pointer_swipe_end_event*>(data);

#ifdef DEBUG
    if (core->debug_pointer)
    {
        wlr_log(WLR_INFO, "[POINTER] swipe_end: cancelled=%d, time_msec=%u",
            (int)event->cancelled, event->time_msec);
    }

#endif
    if (core->input.is_compositor_swipe)
    {
        send_gesture_swipe_end(event->cancelled, event->time_msec);
        core->input.is_compositor_swipe = false;
    } else
    {
        if (core->pointer_gestures)
        {
            wlr_pointer_gestures_v1_send_swipe_end(core->pointer_gestures, core->seat,
                event->time_msec,
                event->cancelled);
        }
    }
}

void handle_pinch_begin(struct wl_listener *listener, void *data)
{
    (void)listener;
    if (sparrow_is_session_locked())
    {
        return;
    }

    const auto core = Core::instance();
    struct wlr_pointer_pinch_begin_event *event =
        static_cast<wlr_pointer_pinch_begin_event*>(data);
#ifdef DEBUG
    if (core->debug_pointer)
    {
        wlr_log(WLR_INFO, "[POINTER] pinch_begin: fingers=%u, time_msec=%u",
            event->fingers, event->time_msec);
    }

#endif
    if (core->pointer_gestures)
    {
        wlr_pointer_gestures_v1_send_pinch_begin(core->pointer_gestures, core->seat,
            event->time_msec, event->fingers);
    }

    core->input.zoom_started = true;
    core->input.scale    = 1.0;
    core->input.rotation = 0.0;
    const auto timestamp = seat_get_flutter_timestamp(event->time_msec);
    send_flutter_trackpad_event(kPanZoomStart, timestamp, 0.0, 0.0, 1.0, 0.0);
}

void handle_pinch_update(struct wl_listener *listener, void *data)
{
    (void)listener;
    if (sparrow_is_session_locked())
    {
        return;
    }

    const auto core = Core::instance();
    struct wlr_pointer_pinch_update_event *event =
        static_cast<wlr_pointer_pinch_update_event*>(data);

#ifdef DEBUG
    if (core->debug_pointer)
    {
        wlr_log(WLR_INFO,
            "[POINTER] pinch_update: dx=%.2f, dy=%.2f, scale=%.4f, rot=%.4f, time_msec=%u",
            event->dx, event->dy, event->scale, event->rotation, event->time_msec);
    }

#endif

    if (core->pointer_gestures)
    {
        wlr_pointer_gestures_v1_send_pinch_update(
            core->pointer_gestures, core->seat, event->time_msec, event->dx,
            event->dy, event->scale, event->rotation);
    }

    core->input.scale    = event->scale;
    core->input.rotation = event->rotation;
    const auto timestamp = seat_get_flutter_timestamp(event->time_msec);
    send_flutter_trackpad_event(kPanZoomUpdate, timestamp, core->input.pan_x,
        core->input.pan_y, core->input.scale,
        core->input.rotation);
}

void handle_pinch_end(struct wl_listener *listener, void *data)
{
    (void)listener;
    if (sparrow_is_session_locked())
    {
        return;
    }

    const auto core = Core::instance();
    struct wlr_pointer_pinch_end_event *event =
        static_cast<wlr_pointer_pinch_end_event*>(data);

#ifdef DEBUG
    if (core->debug_pointer)
    {
        wlr_log(WLR_INFO, "[POINTER] pinch_end: cancelled=%d, time_msec=%u",
            (int)event->cancelled, event->time_msec);
    }

#endif

    if (core->pointer_gestures)
    {
        wlr_pointer_gestures_v1_send_pinch_end(core->pointer_gestures, core->seat,
            event->time_msec, event->cancelled);
    }

    core->input.zoom_started = false;
    const auto timestamp = seat_get_flutter_timestamp(event->time_msec);
    send_flutter_trackpad_event(kPanZoomEnd, timestamp, core->input.pan_x,
        core->input.pan_y, core->input.scale,
        core->input.rotation);
}

void handle_hold_begin(struct wl_listener *listener, void *data)
{
    (void)listener;
    if (sparrow_is_session_locked())
    {
        return;
    }

    const auto core = Core::instance();
    struct wlr_pointer_hold_begin_event *event =
        static_cast<wlr_pointer_hold_begin_event*>(data);

#ifdef DEBUG
    if (core->debug_pointer)
    {
        wlr_log(WLR_INFO, "[POINTER] hold_begin: fingers=%u, time_msec=%u",
            event->fingers, event->time_msec);
    }

#endif

    if (core->pointer_gestures)
    {
        wlr_pointer_gestures_v1_send_hold_begin(core->pointer_gestures, core->seat,
            event->time_msec, event->fingers);
    }
}

void handle_hold_end(struct wl_listener *listener, void *data)
{
    (void)listener;
    if (sparrow_is_session_locked())
    {
        return;
    }

    const auto core = Core::instance();
    struct wlr_pointer_hold_end_event *event =
        static_cast<wlr_pointer_hold_end_event*>(data);

#ifdef DEBUG
    if (core->debug_pointer)
    {
        wlr_log(WLR_INFO, "[POINTER] hold_end: cancelled=%d, time_msec=%u",
            (int)event->cancelled, event->time_msec);
    }

#endif

    if (core->pointer_gestures)
    {
        wlr_pointer_gestures_v1_send_hold_end(core->pointer_gestures, core->seat,
            event->time_msec, event->cancelled);
    }
}

static void client_cursor_handle_destroy(struct wl_listener *listener,
    void *data)
{
    (void)listener;
    (void)data;
    sparrow_cursor_reset_to_flutter();
}

void on_seat_request_cursor(struct wl_listener *listener, void *data)
{
    (void)listener;
    const auto instance = Core::instance();
    // const SparrowView *focused = seat_get_focus();
    // reset_cursor_hide_timer(instance, focused);
    struct wlr_seat_pointer_request_set_cursor_event *event =
        static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);

    const struct wlr_seat_client *focused_client =
        instance->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client)
    {
        if ((instance->client_cursor_destroy.link.next != nullptr) &&
            (instance->client_cursor_destroy.link.prev != nullptr))
        {
            wl_list_remove(&instance->client_cursor_destroy.link);
            wl_list_init(&instance->client_cursor_destroy.link);
        }

        // Track client cursor for software rendering
        instance->client_cursor_surface   = event->surface;
        instance->client_cursor_hotspot_x = event->hotspot_x;
        instance->client_cursor_hotspot_y = event->hotspot_y;

        if (!instance->cursor_visible)
        {
            if (instance->cursor)
            {
                wlr_cursor_unset_image(instance->cursor);
            }
        } else if (event->surface != nullptr)
        {
            wlr_log(
                WLR_INFO,
                "[CURSOR] Client request_set_cursor: surface=%p (%dx%d, scale=%d) hotspot=(%d,%d)",
                static_cast<const void*>(event->surface),
                event->surface->current.width,
                event->surface->current.height,
                event->surface->current.scale,
                event->hotspot_x, event->hotspot_y);
            instance->client_cursor_destroy.notify = client_cursor_handle_destroy;
            wl_signal_add(&event->surface->events.destroy,
                &instance->client_cursor_destroy);
            // Clear xcursor name when using client surface
            instance->current_xcursor_name.clear();
            wlr_cursor_set_surface(instance->cursor, event->surface, event->hotspot_x,
                event->hotspot_y);
        } else
        {
            wlr_log(WLR_INFO, "[CURSOR] Client requested cursor hide (surface=null)");
            instance->current_xcursor_name.clear();
            if (instance->cursor)
            {
                wlr_cursor_unset_image(instance->cursor);
            }
        }

        // Schedule a frame to update the cursor on the screen
        Output *output;
        wl_list_for_each(output, &instance->outputs, link)
        {
            if (output->wlr_output->enabled && output->wlr_output->needs_frame)
            {
                wlr_output_schedule_frame(output->wlr_output);
            }
        }
    }
}

void on_seat_request_set_cursor_shape(struct wl_listener *listener,
    void *data)
{
    (void)listener;
    const auto instance = Core::instance();
    auto *event =
        static_cast<struct wlr_cursor_shape_manager_v1_request_set_shape_event*>(
            data);

    const struct wlr_seat_client *focused_client =
        instance->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client)
    {
        const char *shape_name = wlr_cursor_shape_v1_name(event->shape);
        if (shape_name != nullptr)
        {
            instance->current_xcursor_name = shape_name;

            if ((instance->client_cursor_destroy.link.next != nullptr) &&
                (instance->client_cursor_destroy.link.prev != nullptr))
            {
                wl_list_remove(&instance->client_cursor_destroy.link);
                wl_list_init(&instance->client_cursor_destroy.link);
            }

            instance->client_cursor_surface = nullptr;

            if (!instance->cursor_visible)
            {
                if (instance->cursor)
                {
                    wlr_cursor_unset_image(instance->cursor);
                }
            } else
            {
                wlr_cursor_set_xcursor(instance->cursor, instance->cursor_mgr,
                    shape_name);
            }

            // Schedule a frame to update the cursor on the screen
            Output *output = nullptr;
            wl_list_for_each(output, &instance->outputs, link)
            {
                if (output->wlr_output && output->wlr_output->enabled &&
                    output->wlr_output->needs_frame)
                {
                    wlr_output_schedule_frame(output->wlr_output);
                }
            }
        }
    }
}

static void pointer_destroy(struct wl_listener *listener, void *data)
{
    struct sparrow_pointer *pointer = wl_container_of(listener, pointer, destroy);
    const auto instance = Core::instance();

    wl_list_remove(&pointer->link);
    wlr_cursor_detach_input_device(instance->cursor, &pointer->pointer->base);
    wl_list_remove(&pointer->destroy.link);
    delete pointer;

    sparrow_seat_update_capabilities();
}

void server_new_pointer(struct wlr_input_device *device)
{
    const auto instance = Core::instance();

    wlr_cursor_attach_input_device(instance->cursor, device);

    seat_configure_libinput_device(device);

    struct wlr_pointer *wlr_pointer = wlr_pointer_from_input_device(device);

    struct sparrow_pointer *pointer = new sparrow_pointer();
    if (!pointer)
    {
        wlr_log(WLR_ERROR, "Failed to allocate sparrow_pointer");
        exit(1);
    }

    pointer->pointer = wlr_pointer;
    pointer->device  = device;

    // wlr_pointer_init(wlr_pointer, &pointer_impl, "stipc_pointer");

    pointer->destroy.notify = pointer_destroy;
    wl_signal_add(&wlr_pointer->base.events.destroy, &pointer->destroy);

    wl_list_insert(&instance->pointers, &pointer->link);
}

void handle_new_virtual_pointer(struct wl_listener *listener, void *data)
{
    const auto instance = Core::instance();
    struct wlr_virtual_pointer_v1_new_pointer_event *event =
        static_cast<struct wlr_virtual_pointer_v1_new_pointer_event*>(data);
    struct wlr_virtual_pointer_v1 *virtual_pointer = event->new_pointer;

    wlr_cursor_attach_input_device(instance->cursor,
        &virtual_pointer->pointer.base);

    struct sparrow_pointer *pointer = new sparrow_pointer();
    if (!pointer)
    {
        wlr_log(WLR_ERROR,
            "Failed to allocate sparrow_pointer for virtual pointer");
        return;
    }

    pointer->pointer = &virtual_pointer->pointer;
    pointer->device  = &virtual_pointer->pointer.base;

    pointer->destroy.notify = pointer_destroy;
    wl_signal_add(&virtual_pointer->pointer.base.events.destroy,
        &pointer->destroy);

    wl_list_insert(&instance->pointers, &pointer->link);
    sparrow_seat_update_capabilities();
    wlr_log(WLR_INFO, "New virtual pointer attached and registered");
}
