#include "output.hpp"
#include "util/handle_map.hpp"
#include <algorithm>
#include <core.hpp>
#include <flutter/platform/cursor.hpp>
#include <flutter/platform/pigeon/messages.h>
#include <input/pointer.hpp>
#include <input/seat.hpp>
#include <surface/layer_surface.hpp>
#include <surface/popup.hpp>
#include <surface/session_lock.hpp>
#include <surface/view.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
void sparrow_handle_surface_pointer_event(
    const sparrow::SurfacePointerEventMessage & message)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle((uint32_t)message.surface_handle());
    if (view == nullptr)
    {
        return;
    }

    struct wlr_surface *parent_surface = view->xdg_surface->surface;
    if (parent_surface == nullptr)
    {
        return;
    }

    // 1. Scale Flutter widget coordinates to parent surface coordinate space
    double parent_x = message.local_pos_x();
    double parent_y = message.local_pos_y();

    if ((message.widget_size_x() > 0.0) && (message.widget_size_y() > 0.0))
    {
        const int vis_w =
            (view->width > 0) ? view->width : parent_surface->current.width;
        const int vis_h =
            (view->height > 0) ? view->height : parent_surface->current.height;
        if ((vis_w > 0) && (vis_h > 0))
        {
            const double sx = (double)vis_w / message.widget_size_x();
            const double sy = (double)vis_h / message.widget_size_y();
            if (isfinite(sx) && isfinite(sy) && (sx > 0.0) && (sy > 0.0))
            {
                parent_x = (double)view->geo_x + (message.local_pos_x() * sx);
                parent_y = (double)view->geo_y + (message.local_pos_y() * sy);
            }
        }
    }

    // 2. Resolve subsurfaces - use wlr_surface_surface_at to find the actual
    // surface at the scaled pointer position (may be a subsurface within the
    // parent)
    double sub_x = parent_x;
    double sub_y = parent_y;
    struct wlr_surface *surface = wlr_surface_surface_at(
        parent_surface, parent_x, parent_y, &sub_x, &sub_y);
    if (surface == nullptr)
    {
        // No surface at this position, use parent with scaled coordinates
        surface = parent_surface;
        sub_x   = parent_x;
        sub_y   = parent_y;
    }

    double local_x = sub_x;
    double local_y = sub_y;

    uint32_t time_msec = (uint32_t)(message.timestamp() / 1000);

    // Track Flutter cursor position for grab operations
    instance->input.flutter_cursor_x = message.global_pos_x();
    instance->input.flutter_cursor_y = message.global_pos_y();

    if (PointerDeviceKind::touch == to_device_kind(message.kind()))
    {
        const int32_t touch_id = (int32_t)(message.device() - 1);
        auto point = seat_touch_point_get(touch_id);
        const uint32_t touch_time_msec = point ? point->time_msec : time_msec;

#ifdef DEBUG
        if (instance->debug_touch)
        {
            wlr_log(WLR_INFO,
                "[TOUCH] handle_surface_touch: surface=%u, touch_id=%d, pos=(%.2f, %.2f), ev_type=%d, time_msec=%u",
                (uint32_t)message.surface_handle(), touch_id, local_x, local_y,
                (int)message.event_type(), touch_time_msec);
        }

#endif

#ifdef DEBUG
        if (instance->debug_touch)
        {
            wlr_log(WLR_INFO,
                "[TOUCH] handle_surface_touch: surface=%u, touch_id=%d, pos=(%.2f, %.2f), ev_type=%d, time_msec=%u",
                (uint32_t)message.surface_handle(), touch_id, local_x, local_y,
                (int)message.event_type(), touch_time_msec);
        }

#endif

        if (message.event_type() == pointerMoveEvent)
        {
            if (surface)
            {
#ifdef DEBUG
                if (instance->debug_touch)
                {
                    wlr_log(WLR_INFO,
                        "[TOUCH] touch_notify_motion: surface=%u, id=%d, pos=(%.2f, %.2f), time_msec=%u",
                        (uint32_t)message.surface_handle(), touch_id, local_x, local_y, touch_time_msec);
                }

#endif
#ifdef DEBUG
                if (instance->debug_touch)
                {
                    wlr_log(WLR_INFO,
                        "[TOUCH] touch_notify_motion: surface=%u, id=%d, pos=(%.2f, %.2f), time_msec=%u",
                        (uint32_t)message.surface_handle(), touch_id, local_x, local_y, touch_time_msec);
                }

#endif
                wlr_seat_touch_notify_motion(instance->seat, touch_time_msec, touch_id,
                    local_x, local_y);
            } else
            {
                wlr_seat_touch_point_clear_focus(instance->seat, touch_time_msec,
                    touch_id);
            }

            wlr_seat_touch_notify_frame(instance->seat);

            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerDownEvent)
        {
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] touch_notify_down: surface=%u, id=%d, pos=(%.2f, %.2f), time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, local_x, local_y, touch_time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] touch_notify_down: surface=%u, id=%d, pos=(%.2f, %.2f), time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, local_x, local_y, touch_time_msec);
            }

#endif
            auto serial = wlr_seat_touch_notify_down(
                instance->seat, surface, touch_time_msec, touch_id, local_x, local_y);

            if (serial && (wlr_seat_touch_num_points(instance->seat) == 1))
            {
                sparrow_view_focus(view);
                if (view->scene_tree != nullptr)
                {
                    wlr_scene_node_raise_to_top(&view->scene_tree->node);
                }
            }

            wlr_seat_touch_notify_frame(instance->seat);

            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerUpEvent)
        {
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] touch_notify_up: surface=%u, id=%d, time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, touch_time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] touch_notify_up: surface=%u, id=%d, time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, touch_time_msec);
            }

#endif
            if (wlr_seat_touch_num_points(instance->seat) == 1)
            {
                sparrow_view_focus(view);
                if (view->scene_tree != nullptr)
                {
                    wlr_scene_node_raise_to_top(&view->scene_tree->node);
                }
            }

            wlr_seat_touch_notify_up(instance->seat, touch_time_msec, touch_id);
            wlr_seat_touch_notify_frame(instance->seat);

            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
            seat_touch_point_delete(touch_id);
        } else if (message.event_type() == pointerCancelEvent)
        {
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] touch_notify_cancel: surface=%u, id=%d, time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, touch_time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] touch_notify_cancel: surface=%u, id=%d, time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, touch_time_msec);
            }

#endif
            wlr_seat_touch_point_clear_focus(instance->seat, touch_time_msec,
                touch_id);
            wlr_seat_touch_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
            seat_touch_point_delete(touch_id);
        }
    } else
    {
        if (message.event_type() == pointerEnterEvent)
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
            wlr_seat_pointer_notify_frame(instance->seat);
            sparrow_pointer_constraints_set_focus(instance, surface);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerExitEvent)
        {
            sparrow_pointer_constraints_deactivate(instance);
            wlr_seat_pointer_clear_focus(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
            sparrow_cursor_reset_to_flutter();

            // Don't clear button state on exit - track by surface handle instead
        } else if ((message.event_type() == pointerHoverEvent) ||
                   (message.event_type() == pointerMoveEvent))
        {
            if ((instance->direct_input_mode ||
                 (instance->input.direct_mouse_button_mask != 0)) &&
                (instance->direct_input_surface == message.surface_handle()))
            {
                // In direct input mode, motion was already dispatched with 0ms latency in
                // process_cursor_motion.
                return;
            }

            if (!instance->input.motions.empty())
            {
                for (auto it = instance->input.motions.begin();
                     it != instance->input.motions.end();)
                {
                    {
                        auto motion = *it;

                        wlr_seat_pointer_notify_enter(instance->seat, surface, local_x,
                            local_y);
                        wlr_seat_pointer_notify_motion(instance->seat, motion.time_msec,
                            local_x, local_y);

                        if ((motion.dx != 0) || (motion.dy != 0))
                        {
                            wlr_relative_pointer_manager_v1_send_relative_motion(
                                instance->relative_pointer_manager, instance->seat,
                                (uint64_t)time_msec * 1000, motion.dx, motion.dy,
                                motion.dx_unaccel, motion.dy_unaccel);
                        }

                        wlr_seat_pointer_notify_frame(instance->seat);
                        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                            instance->seat);
                        ++it;
                    }
                }

                instance->input.motions.clear();
            } else
            {
                wlr_seat_pointer_notify_enter(instance->seat, surface, local_x,
                    local_y);
                wlr_seat_pointer_notify_motion(instance->seat, time_msec, local_x,
                    local_y);
                wlr_seat_pointer_notify_frame(instance->seat);
                wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                    instance->seat);
            }

            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if ((message.event_type() == pointerDownEvent) ||
                   (message.event_type() == pointerUpEvent))
        {
            sparrow_view_focus(view);
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
            wlr_seat_pointer_notify_motion(instance->seat, time_msec, local_x,
                local_y);
            wlr_seat_pointer_notify_frame(instance->seat);
            if (view->scene_tree != nullptr)
            {
                wlr_scene_node_raise_to_top(&view->scene_tree->node);
            }

            // Use cached wlroots timestamp for accurate Wayland timing
            // (Flutter's timestamp is from a different clock)
            const uint32_t button_time =
                instance->input.last_button.valid ?
                instance->input.last_button.time_msec :
                time_msec; // Fallback to Flutter timestamp if no cache
            instance->input.last_button.valid = false;

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_button: surface=%u, ev_type=%s, pos=(%.2f, %.2f), btns=0x%"
                    PRIx64 ", time_msec=%u",
                    (uint32_t)message.surface_handle(),
                    message.event_type() == pointerDownEvent ? "pointerDownEvent" : "pointerUpEvent",
                    local_x, local_y, (uint64_t)message.buttons(), button_time);
            }

#endif

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_button: surface=%u, ev_type=%s, pos=(%.2f, %.2f), btns=0x%"
                    PRIx64 ", time_msec=%u",
                    (uint32_t)message.surface_handle(),
                    message.event_type() == pointerDownEvent ? "pointerDownEvent" : "pointerUpEvent",
                    local_x, local_y, (uint64_t)message.buttons(), button_time);
            }

#endif

            // Track button state by surface handle (more reliable than pointer ID
            // which can change)
            const int64_t prev_buttons = get_surface_buttons(message.surface_handle());
            const int64_t next_buttons = message.buttons();
            const int64_t changed = prev_buttons ^ next_buttons;

            for (int bit = 0; bit < 16; bit++)
            {
                int64_t flutter_button = (1LL << bit);
                if ((changed & flutter_button) == 0)
                {
                    continue;
                }

                uint32_t linux_button;
                if (!flutter_mouse_button_to_linux(flutter_button, &linux_button))
                {
                    continue;
                }

                enum wl_pointer_button_state state =
                    (next_buttons & flutter_button) ? WL_POINTER_BUTTON_STATE_PRESSED :
                    WL_POINTER_BUTTON_STATE_RELEASED;

#ifdef DEBUG
                if (instance->debug_pointer)
                {
                    wlr_log(WLR_INFO,
                        "[POINTER] notify_button: linux_btn=0x%x (%u), state=%d, time_msec=%u",
                        linux_button, linux_button, (int)state, button_time);
                }

#endif

#ifdef DEBUG
                if (instance->debug_pointer)
                {
                    wlr_log(WLR_INFO,
                        "[POINTER] notify_button: linux_btn=0x%x (%u), state=%d, time_msec=%u",
                        linux_button, linux_button, (int)state, button_time);
                }

#endif

                wlr_seat_pointer_notify_button(instance->seat, button_time,
                    linux_button, state);
            }

            set_surface_buttons(message.surface_handle(), next_buttons);
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerScrollEvent)
        {
            // Flutter-first scroll: Flutter receives scroll from embedder, does
            // hit-testing, and forwards to the correct surface via this platform
            // channel handler. NOTE: Don't call notify_enter here - seat already has
            // focus from motion events.
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_scroll: surface=%u, cached_scrolls=%zu, msg_delta=(%.2f, %.2f), time_msec=%u",
                    (uint32_t)message.surface_handle(), instance->input.scrolls.size(),
                    message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_scroll: surface=%u, cached_scrolls=%zu, msg_delta=(%.2f, %.2f), time_msec=%u",
                    (uint32_t)message.surface_handle(), instance->input.scrolls.size(),
                    message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
            }

#endif
            if (!instance->input.scrolls.empty())
            {
                for (const auto & cache : instance->input.scrolls)
                {
#ifdef DEBUG
                    if (instance->debug_pointer)
                    {
                        wlr_log(WLR_INFO,
                            "[POINTER] notify_axis (cache): orient=%s, delta=%.4f, disc=%d, src=%d, rel_dir=%d, time_msec=%u",
                            (cache.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
                            cache.delta, cache.delta_discrete, (int)cache.source,
                            (int)cache.relative_direction, cache.time_msec);
                    }

#endif
#ifdef DEBUG
                    if (instance->debug_pointer)
                    {
                        wlr_log(WLR_INFO,
                            "[POINTER] notify_axis (cache): orient=%s, delta=%.4f, disc=%d, src=%d, rel_dir=%d, time_msec=%u",
                            (cache.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
                            cache.delta, cache.delta_discrete, (int)cache.source,
                            (int)cache.relative_direction, cache.time_msec);
                    }

#endif
                    wlr_seat_pointer_notify_axis(
                        instance->seat, cache.time_msec, cache.orientation, cache.delta,
                        cache.delta_discrete, cache.source, cache.relative_direction);
                    wlr_seat_pointer_notify_frame(instance->seat);
                }

                instance->input.scrolls.clear();
            } else
            {
#ifdef DEBUG
                if (instance->debug_pointer)
                {
                    wlr_log(WLR_INFO,
                        "[POINTER] notify_axis (fallback): delta=(%.2f, %.2f), src=WHEEL, time_msec=%u",
                        message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
                }

#endif
#ifdef DEBUG
                if (instance->debug_pointer)
                {
                    wlr_log(WLR_INFO,
                        "[POINTER] notify_axis (fallback): delta=(%.2f, %.2f), src=WHEEL, time_msec=%u",
                        message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
                }

#endif
                if (message.scroll_delta_y() != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                        -message.scroll_delta_y(), 0, WL_POINTER_AXIS_SOURCE_WHEEL,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }

                if (message.scroll_delta_x() != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                        -message.scroll_delta_x(), 0, WL_POINTER_AXIS_SOURCE_WHEEL,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }
            }

            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerPanZoomStartEvent)
        {
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_pan_zoom_start: surface=%u, time_msec=%u",
                    (uint32_t)message.surface_handle(), time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_pan_zoom_start: surface=%u, time_msec=%u",
                    (uint32_t)message.surface_handle(), time_msec);
            }

#endif
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerPanZoomUpdateEvent)
        {
            const bool is_pinch_gesture = (message.scale() != 1.0) ||
                (message.rotation() != 0.0) ||
                instance->input.zoom_started;

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_pan_zoom_update: surface=%u, cached_scrolls=%zu, is_pinch=%d, pan=(%.2f, %.2f), scale=%.4f, rot=%.4f, time_msec=%u",
                    (uint32_t)message.surface_handle(), instance->input.scrolls.size(),
                    (int)is_pinch_gesture, message.pan_x(), message.pan_y(), message.scale(),
                    message.rotation(), time_msec);
            }

#endif

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_pan_zoom_update: surface=%u, cached_scrolls=%zu, is_pinch=%d, pan=(%.2f, %.2f), scale=%.4f, rot=%.4f, time_msec=%u",
                    (uint32_t)message.surface_handle(), instance->input.scrolls.size(),
                    (int)is_pinch_gesture, message.pan_x(), message.pan_y(), message.scale(),
                    message.rotation(), time_msec);
            }

#endif

            if (!is_pinch_gesture)
            {
                if (!instance->input.scrolls.empty())
                {
                    for (const auto & cache : instance->input.scrolls)
                    {
#ifdef DEBUG
                        if (instance->debug_pointer)
                        {
                            wlr_log(WLR_INFO,
                                "[POINTER] notify_axis pan (cache): orient=%s, delta=%.4f, disc=%d, src=%d, rel_dir=%d, time_msec=%u",
                                (cache.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
                                cache.delta, cache.delta_discrete, (int)cache.source,
                                (int)cache.relative_direction, cache.time_msec);
                        }

#endif
#ifdef DEBUG
                        if (instance->debug_pointer)
                        {
                            wlr_log(WLR_INFO,
                                "[POINTER] notify_axis pan (cache): orient=%s, delta=%.4f, disc=%d, src=%d, rel_dir=%d, time_msec=%u",
                                (cache.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
                                cache.delta, cache.delta_discrete, (int)cache.source,
                                (int)cache.relative_direction, cache.time_msec);
                        }

#endif
                        wlr_seat_pointer_notify_axis(
                            instance->seat, cache.time_msec, cache.orientation, cache.delta,
                            cache.delta_discrete, cache.source, cache.relative_direction);
                        wlr_seat_pointer_notify_frame(instance->seat);
                    }

                    instance->input.scrolls.clear();
                } else
                {
#ifdef DEBUG
                    if (instance->debug_pointer)
                    {
                        wlr_log(WLR_INFO,
                            "[POINTER] notify_axis pan (fallback): delta=(%.2f, %.2f), src=FINGER, time_msec=%u",
                            message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
                    }

#endif
#ifdef DEBUG
                    if (instance->debug_pointer)
                    {
                        wlr_log(WLR_INFO,
                            "[POINTER] notify_axis pan (fallback): delta=(%.2f, %.2f), src=FINGER, time_msec=%u",
                            message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
                    }

#endif
                    if (message.scroll_delta_y() != 0.0)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                            -message.scroll_delta_y(), 0, WL_POINTER_AXIS_SOURCE_FINGER,
                            WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                    }

                    if (message.scroll_delta_x() != 0.0)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                            -message.scroll_delta_x(), 0, WL_POINTER_AXIS_SOURCE_FINGER,
                            WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                    }
                }

                wlr_seat_pointer_notify_frame(instance->seat);
                wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                    instance->seat);
            }
        } else if (message.event_type() == pointerPanZoomEndEvent)
        {
            const bool is_pinch_gesture = instance->input.zoom_started;
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_pan_zoom_end: surface=%u, is_pinch=%d, time_msec=%u -> notify_axis stop (vert & horiz)",
                    (uint32_t)message.surface_handle(), (int)is_pinch_gesture, time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_surface_pan_zoom_end: surface=%u, is_pinch=%d, time_msec=%u -> notify_axis stop (vert & horiz)",
                    (uint32_t)message.surface_handle(), (int)is_pinch_gesture, time_msec);
            }

#endif
            if (!is_pinch_gesture)
            {
                // Notify axis stop to trigger client inertial scrolling
                wlr_seat_pointer_notify_axis(
                    instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL, 0, 0,
                    WL_POINTER_AXIS_SOURCE_FINGER,
                    WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                wlr_seat_pointer_notify_axis(
                    instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL, 0, 0,
                    WL_POINTER_AXIS_SOURCE_FINGER,
                    WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                wlr_seat_pointer_notify_frame(instance->seat);
                wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                    instance->seat);
            }
        }
    }
}

void sparrow_handle_popup_pointer_event(
    const sparrow::SurfacePointerEventMessage & message)
{
    const auto instance = Core::instance();

    wlr_log(WLR_DEBUG, "popup_pointer_event: decoded handle=%d type=%d",
        (int)message.surface_handle(), (int)message.event_type());

    SparrowPopup *popup;
    if (!handle_map_get(instance->popups, (uint32_t)message.surface_handle(),
        (void**)&popup) ||
        (popup == nullptr))
    {
        wlr_log(WLR_DEBUG, "popup_pointer_event: popup handle %d not found",
            (int)message.surface_handle());
        return;
    }

    struct wlr_surface *popup_surface = popup->xdg_surface->surface;
    if (popup_surface == nullptr)
    {
        return;
    }

    const uint32_t time_msec = (uint32_t)(message.timestamp() / 1000);

    // Find the actual surface under cursor - Firefox renders content to
    // subsurfaces
    double local_x = message.local_pos_x();
    double local_y = message.local_pos_y();

    if ((message.widget_size_x() > 0.0) && (message.widget_size_y() > 0.0))
    {
        const int vis_w =
            (popup->width > 0) ? popup->width : popup_surface->current.width;
        const int vis_h =
            (popup->height > 0) ? popup->height : popup_surface->current.height;
        if ((vis_w > 0) && (vis_h > 0))
        {
            const double sx = (double)vis_w / message.widget_size_x();
            const double sy = (double)vis_h / message.widget_size_y();
            if (isfinite(sx) && isfinite(sy) && (sx > 0.0) && (sy > 0.0))
            {
                local_x = message.local_pos_x() * sx;
                local_y = message.local_pos_y() * sy;
            }
        }
    }

    double sub_x, sub_y;
    struct wlr_surface *surface =
        wlr_surface_surface_at(popup_surface, local_x, local_y, &sub_x, &sub_y);
    if (surface != nullptr)
    {
        // Use coordinates relative to the found surface (possibly a subsurface)
        local_x = sub_x;
        local_y = sub_y;
    } else
    {
        // Fallback to popup surface if nothing found at coordinates
        surface = popup_surface;
    }

    if ((surface != nullptr) && (surface->current.width > 0) &&
        (surface->current.height > 0))
    {
        local_x =
            std::clamp(local_x, 0.0, static_cast<double>(surface->current.width));
        local_y =
            std::clamp(local_y, 0.0, static_cast<double>(surface->current.height));
    }

    wlr_log(
        WLR_DEBUG,
        "Popup %d pointer event type=%d at (%.1f,%.1f) -> surface=%p (%.1f,%.1f)",
        (int)message.surface_handle(), (int)message.event_type(), message.local_pos_x(),
        message.local_pos_y(), static_cast<void*>(surface), local_x, local_y);

    if (PointerDeviceKind::touch == to_device_kind(message.kind()))
    {
        const int32_t touch_id = (int32_t)(message.device() - 1);
        auto point = seat_touch_point_get(touch_id);
        const uint32_t touch_time_msec = point ? point->time_msec : time_msec;

#ifdef DEBUG
        if (instance->debug_touch)
        {
            wlr_log(WLR_INFO,
                "[TOUCH] handle_popup_touch: popup=%u, touch_id=%d, pos=(%.2f, %.2f), ev_type=%d, time_msec=%u",
                (uint32_t)message.surface_handle(), touch_id, local_x, local_y,
                (int)message.event_type(), touch_time_msec);
        }

#endif

#ifdef DEBUG
        if (instance->debug_touch)
        {
            wlr_log(WLR_INFO,
                "[TOUCH] handle_popup_touch: popup=%u, touch_id=%d, pos=(%.2f, %.2f), ev_type=%d, time_msec=%u",
                (uint32_t)message.surface_handle(), touch_id, local_x, local_y,
                (int)message.event_type(), touch_time_msec);
        }

#endif

        if (message.event_type() == pointerMoveEvent)
        {
            if (surface)
            {
#ifdef DEBUG
                if (instance->debug_touch)
                {
                    wlr_log(WLR_INFO,
                        "[TOUCH] popup_touch_notify_motion: popup=%u, id=%d, pos=(%.2f, %.2f), time_msec=%u",
                        (uint32_t)message.surface_handle(), touch_id, local_x, local_y, touch_time_msec);
                }

#endif
#ifdef DEBUG
                if (instance->debug_touch)
                {
                    wlr_log(WLR_INFO,
                        "[TOUCH] popup_touch_notify_motion: popup=%u, id=%d, pos=(%.2f, %.2f), time_msec=%u",
                        (uint32_t)message.surface_handle(), touch_id, local_x, local_y, touch_time_msec);
                }

#endif
                wlr_seat_touch_notify_motion(instance->seat, touch_time_msec, touch_id,
                    local_x, local_y);
            } else
            {
                wlr_seat_touch_point_clear_focus(instance->seat, touch_time_msec,
                    touch_id);
            }

            wlr_seat_touch_notify_frame(instance->seat);

            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerDownEvent)
        {
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] popup_touch_notify_down: popup=%u, id=%d, pos=(%.2f, %.2f), time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, local_x, local_y, touch_time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] popup_touch_notify_down: popup=%u, id=%d, pos=(%.2f, %.2f), time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, local_x, local_y, touch_time_msec);
            }

#endif
            auto serial = wlr_seat_touch_notify_down(
                instance->seat, surface, touch_time_msec, touch_id, local_x, local_y);

            if (serial && (wlr_seat_touch_num_points(instance->seat) == 1))
            {
                sparrow_focus_popup(popup);
                if (popup->scene_tree != nullptr)
                {
                    wlr_scene_node_raise_to_top(&popup->scene_tree->node);
                }
            }

            wlr_seat_touch_notify_frame(instance->seat);

            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerUpEvent)
        {
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] popup_touch_notify_up: popup=%u, id=%d, time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, touch_time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] popup_touch_notify_up: popup=%u, id=%d, time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, touch_time_msec);
            }

#endif
            if (wlr_seat_touch_num_points(instance->seat) == 1)
            {
                sparrow_focus_popup(popup);
                if (popup->scene_tree != nullptr)
                {
                    wlr_scene_node_raise_to_top(&popup->scene_tree->node);
                }
            }

            wlr_seat_touch_notify_up(instance->seat, touch_time_msec, touch_id);
            wlr_seat_touch_notify_frame(instance->seat);

            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
            seat_touch_point_delete(touch_id);
        } else if (message.event_type() == pointerCancelEvent)
        {
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] popup_touch_notify_cancel: popup=%u, id=%d, time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, touch_time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_touch)
            {
                wlr_log(WLR_INFO,
                    "[TOUCH] popup_touch_notify_cancel: popup=%u, id=%d, time_msec=%u",
                    (uint32_t)message.surface_handle(), touch_id, touch_time_msec);
            }

#endif
            wlr_seat_touch_point_clear_focus(instance->seat, touch_time_msec,
                touch_id);
            wlr_seat_touch_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
            seat_touch_point_delete(touch_id);
        }
    } else
    {
        if (message.event_type() == pointerEnterEvent)
        {
            sparrow_focus_popup(popup);
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if ((message.event_type() == pointerExitEvent) ||
                   (message.event_type() == pointerPanZoomStartEvent))
        {
#ifdef DEBUG
            if (instance->debug_pointer && (message.event_type() == pointerPanZoomStartEvent))
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_pan_zoom_start: popup=%u, time_msec=%u",
                    (uint32_t)message.surface_handle(), time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_pointer && (message.event_type() == pointerPanZoomStartEvent))
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_pan_zoom_start: popup=%u, time_msec=%u",
                    (uint32_t)message.surface_handle(), time_msec);
            }

#endif
            // Don't clear focus on popup exit - let the parent or next popup handle
            // it
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if ((message.event_type() == pointerHoverEvent) ||
                   (message.event_type() == pointerMoveEvent))
        {
            sparrow_focus_popup(popup);
            if (instance->seat->pointer_state.focused_surface != surface)
            {
                wlr_seat_pointer_notify_enter(instance->seat, surface, local_x,
                    local_y);
            }

            if (!instance->input.motions.empty())
            {
                for (auto it = instance->input.motions.begin();
                     it != instance->input.motions.end();)
                {
                    {
                        auto motion = *it;
                        wlr_seat_pointer_notify_motion(instance->seat, motion.time_msec,
                            local_x, local_y);

                        if ((motion.dx != 0) || (motion.dy != 0))
                        {
                            wlr_relative_pointer_manager_v1_send_relative_motion(
                                instance->relative_pointer_manager, instance->seat,
                                (uint64_t)time_msec * 1000, motion.dx, motion.dy,
                                motion.dx_unaccel, motion.dy_unaccel);
                        }

                        wlr_seat_pointer_notify_frame(instance->seat);
                        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                            instance->seat);
                        ++it;
                    }
                }

                instance->input.motions.clear();
            } else
            {
                wlr_seat_pointer_notify_motion(instance->seat, time_msec, local_x,
                    local_y);
                wlr_seat_pointer_notify_frame(instance->seat);
                wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                    instance->seat);
            }
        } else if ((message.event_type() == pointerDownEvent) ||
                   (message.event_type() == pointerUpEvent))
        {
            sparrow_focus_popup(popup);
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
            wlr_seat_pointer_notify_motion(instance->seat, time_msec, local_x,
                local_y);
            wlr_seat_pointer_notify_frame(instance->seat);
            if (popup->scene_tree != nullptr)
            {
                wlr_scene_node_raise_to_top(&popup->scene_tree->node);
            }

            // Track button state by popup handle
            const int64_t prev_buttons = get_surface_buttons(
                message.surface_handle() +
                200000); // Offset to avoid collision with surface handles
            const int64_t next_buttons = message.buttons();
            const int64_t changed = prev_buttons ^ next_buttons;

            // Use cached button timestamp for accurate Wayland delivery
            const uint32_t button_time = instance->input.last_button.valid ?
                instance->input.last_button.time_msec :
                time_msec;
            instance->input.last_button.valid = false;

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_button: popup=%u, ev_type=%s, pos=(%.2f, %.2f), btns=0x%" PRIx64
                    ", time_msec=%u",
                    (uint32_t)message.surface_handle(),
                    message.event_type() == pointerDownEvent ? "pointerDownEvent" : "pointerUpEvent",
                    local_x, local_y, (uint64_t)message.buttons(), button_time);
            }

#endif

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_button: popup=%u, ev_type=%s, pos=(%.2f, %.2f), btns=0x%" PRIx64
                    ", time_msec=%u",
                    (uint32_t)message.surface_handle(),
                    message.event_type() == pointerDownEvent ? "pointerDownEvent" : "pointerUpEvent",
                    local_x, local_y, (uint64_t)message.buttons(), button_time);
            }

#endif

            for (int bit = 0; bit < 16; bit++)
            {
                int64_t flutter_button = (1LL << bit);
                if ((changed & flutter_button) == 0)
                {
                    continue;
                }

                uint32_t linux_button;
                if (!flutter_mouse_button_to_linux(flutter_button, &linux_button))
                {
                    continue;
                }

                enum wl_pointer_button_state state =
                    (next_buttons & flutter_button) ? WL_POINTER_BUTTON_STATE_PRESSED :
                    WL_POINTER_BUTTON_STATE_RELEASED;

#ifdef DEBUG
                if (instance->debug_pointer)
                {
                    wlr_log(WLR_INFO,
                        "[POINTER] popup notify_button: linux_btn=0x%x (%u), state=%d, time_msec=%u",
                        linux_button, linux_button, (int)state, button_time);
                }

#endif

#ifdef DEBUG
                if (instance->debug_pointer)
                {
                    wlr_log(WLR_INFO,
                        "[POINTER] popup notify_button: linux_btn=0x%x (%u), state=%d, time_msec=%u",
                        linux_button, linux_button, (int)state, button_time);
                }

#endif
                wlr_seat_pointer_notify_button(instance->seat, button_time,
                    linux_button, state);
            }

            set_surface_buttons(message.surface_handle() + 200000, next_buttons);
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerScrollEvent)
        {
            // Flutter-first scroll: Flutter receives scroll from embedder, does
            // hit-testing, and forwards to the correct popup via this platform
            // channel handler. NOTE: Don't call notify_enter here - seat already has
            // focus from motion events.
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_scroll: popup=%u, cached_scrolls=%zu, msg_delta=(%.2f, %.2f), time_msec=%u",
                    (uint32_t)message.surface_handle(), instance->input.scrolls.size(),
                    message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_scroll: popup=%u, cached_scrolls=%zu, msg_delta=(%.2f, %.2f), time_msec=%u",
                    (uint32_t)message.surface_handle(), instance->input.scrolls.size(),
                    message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
            }

#endif
            if (!instance->input.scrolls.empty())
            {
                for (const auto & cache : instance->input.scrolls)
                {
#ifdef DEBUG
                    if (instance->debug_pointer)
                    {
                        wlr_log(WLR_INFO,
                            "[POINTER] popup notify_axis (cache): orient=%s, delta=%.4f, disc=%d, src=%d, rel_dir=%d, time_msec=%u",
                            (cache.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
                            cache.delta, cache.delta_discrete, (int)cache.source,
                            (int)cache.relative_direction, cache.time_msec);
                    }

#endif
#ifdef DEBUG
                    if (instance->debug_pointer)
                    {
                        wlr_log(WLR_INFO,
                            "[POINTER] popup notify_axis (cache): orient=%s, delta=%.4f, disc=%d, src=%d, rel_dir=%d, time_msec=%u",
                            (cache.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
                            cache.delta, cache.delta_discrete, (int)cache.source,
                            (int)cache.relative_direction, cache.time_msec);
                    }

#endif
                    wlr_seat_pointer_notify_axis(
                        instance->seat, cache.time_msec, cache.orientation, cache.delta,
                        cache.delta_discrete, cache.source, cache.relative_direction);
                    wlr_seat_pointer_notify_frame(instance->seat);
                }

                instance->input.scrolls.clear();
            } else
            {
#ifdef DEBUG
                if (instance->debug_pointer)
                {
                    wlr_log(WLR_INFO,
                        "[POINTER] popup notify_axis (fallback): delta=(%.2f, %.2f), src=WHEEL, time_msec=%u",
                        message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
                }

#endif
#ifdef DEBUG
                if (instance->debug_pointer)
                {
                    wlr_log(WLR_INFO,
                        "[POINTER] popup notify_axis (fallback): delta=(%.2f, %.2f), src=WHEEL, time_msec=%u",
                        message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
                }

#endif
                if (message.scroll_delta_y() != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                        -message.scroll_delta_y(), 0, WL_POINTER_AXIS_SOURCE_WHEEL,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }

                if (message.scroll_delta_x() != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                        -message.scroll_delta_x(), 0, WL_POINTER_AXIS_SOURCE_WHEEL,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }
            }

            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type() == pointerPanZoomUpdateEvent)
        {
            const bool is_pinch_gesture = (message.scale() != 1.0) ||
                (message.rotation() != 0.0) ||
                instance->input.zoom_started;

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_pan_zoom_update: popup=%u, cached_scrolls=%zu, is_pinch=%d, pan=(%.2f, %.2f), scale=%.4f, rot=%.4f, time_msec=%u",
                    (uint32_t)message.surface_handle(), instance->input.scrolls.size(),
                    (int)is_pinch_gesture, message.pan_x(), message.pan_y(), message.scale(),
                    message.rotation(), time_msec);
            }

#endif

#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_pan_zoom_update: popup=%u, cached_scrolls=%zu, is_pinch=%d, pan=(%.2f, %.2f), scale=%.4f, rot=%.4f, time_msec=%u",
                    (uint32_t)message.surface_handle(), instance->input.scrolls.size(),
                    (int)is_pinch_gesture, message.pan_x(), message.pan_y(), message.scale(),
                    message.rotation(), time_msec);
            }

#endif

            if (!is_pinch_gesture)
            {
                if (!instance->input.scrolls.empty())
                {
                    for (const auto & cache : instance->input.scrolls)
                    {
#ifdef DEBUG
                        if (instance->debug_pointer)
                        {
                            wlr_log(WLR_INFO,
                                "[POINTER] popup notify_axis pan (cache): orient=%s, delta=%.4f, disc=%d, src=%d, rel_dir=%d, time_msec=%u",
                                (cache.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
                                cache.delta, cache.delta_discrete, (int)cache.source,
                                (int)cache.relative_direction, cache.time_msec);
                        }

#endif
#ifdef DEBUG
                        if (instance->debug_pointer)
                        {
                            wlr_log(WLR_INFO,
                                "[POINTER] popup notify_axis pan (cache): orient=%s, delta=%.4f, disc=%d, src=%d, rel_dir=%d, time_msec=%u",
                                (cache.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vert" : "horiz",
                                cache.delta, cache.delta_discrete, (int)cache.source,
                                (int)cache.relative_direction, cache.time_msec);
                        }

#endif
                        wlr_seat_pointer_notify_axis(
                            instance->seat, cache.time_msec, cache.orientation, cache.delta,
                            cache.delta_discrete, cache.source, cache.relative_direction);
                        wlr_seat_pointer_notify_frame(instance->seat);
                    }

                    instance->input.scrolls.clear();
                } else
                {
#ifdef DEBUG
                    if (instance->debug_pointer)
                    {
                        wlr_log(WLR_INFO,
                            "[POINTER] popup notify_axis pan (fallback): delta=(%.2f, %.2f), src=FINGER, time_msec=%u",
                            message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
                    }

#endif
#ifdef DEBUG
                    if (instance->debug_pointer)
                    {
                        wlr_log(WLR_INFO,
                            "[POINTER] popup notify_axis pan (fallback): delta=(%.2f, %.2f), src=FINGER, time_msec=%u",
                            message.scroll_delta_x(), message.scroll_delta_y(), time_msec);
                    }

#endif
                    if (message.scroll_delta_y() != 0.0)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                            -message.scroll_delta_y(), 0, WL_POINTER_AXIS_SOURCE_FINGER,
                            WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                    }

                    if (message.scroll_delta_x() != 0.0)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                            -message.scroll_delta_x(), 0, WL_POINTER_AXIS_SOURCE_FINGER,
                            WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                    }
                }

                wlr_seat_pointer_notify_frame(instance->seat);
                wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                    instance->seat);
            }
        } else if (message.event_type() == pointerPanZoomEndEvent)
        {
            const bool is_pinch_gesture = instance->input.zoom_started;
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_pan_zoom_end: popup=%u, is_pinch=%d, time_msec=%u -> notify_axis stop (vert & horiz)",
                    (uint32_t)message.surface_handle(), (int)is_pinch_gesture, time_msec);
            }

#endif
#ifdef DEBUG
            if (instance->debug_pointer)
            {
                wlr_log(WLR_INFO,
                    "[POINTER] handle_popup_pan_zoom_end: popup=%u, is_pinch=%d, time_msec=%u -> notify_axis stop (vert & horiz)",
                    (uint32_t)message.surface_handle(), (int)is_pinch_gesture, time_msec);
            }

#endif
            if (!is_pinch_gesture)
            {
                // Notify axis stop to trigger client inertial scrolling
                wlr_seat_pointer_notify_axis(
                    instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL, 0, 0,
                    WL_POINTER_AXIS_SOURCE_FINGER,
                    WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                wlr_seat_pointer_notify_axis(
                    instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL, 0, 0,
                    WL_POINTER_AXIS_SOURCE_FINGER,
                    WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                wlr_seat_pointer_notify_frame(instance->seat);
                wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                    instance->seat);
            }
        }
    }
}

void sparrow_handle_surface_keyboard_key(
    const sparrow::SurfaceKeyboardKeyMessage & message)
{
    if (sparrow_is_session_locked())
    {
        return;
    }

    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle((uint32_t)message.surface_handle());
    if (view == nullptr)
    {
        return;
    }

    sparrow_view_focus(view);

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(instance->seat);
    if (keyboard != nullptr)
    {
        wlr_seat_set_keyboard(instance->seat, keyboard);
    }

    enum wl_keyboard_key_state state = message.status() != 0 ?
        WL_KEYBOARD_KEY_STATE_PRESSED :
        WL_KEYBOARD_KEY_STATE_RELEASED;

    if ((state == WL_KEYBOARD_KEY_STATE_PRESSED) && keyboard &&
        keyboard->xkb_state)
    {
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(
            keyboard->xkb_state, (xkb_keycode_t)message.keycode());
        if (sym == XKB_KEY_F8)
        {
            Output *out = instance->vsync_output ? instance->vsync_output :
                sparrow_get_first_output();
            if (out && out->wlr_output)
            {
                struct wlr_output_state out_state;
                wlr_output_state_init(&out_state);
                bool new_enabled = !out->wlr_output->enabled;
                wlr_output_state_set_enabled(&out_state, new_enabled);
                if (!wlr_output_commit_state(out->wlr_output, &out_state))
                {
                    wlr_log(WLR_ERROR,
                        "[DPMS] Failed to commit power toggle for output '%s'",
                        out->wlr_output->name);
                } else
                {
                    wlr_log(WLR_INFO, "[DPMS] Output '%s' power toggled via F8: %s",
                        out->wlr_output->name, new_enabled ? "ON" : "OFF");
                    if (new_enabled)
                    {
                        sparrow_select_highest_refresh_output();
                        sparrow_damage_add_box(nullptr);
                        wlr_output_schedule_frame(out->wlr_output);
                    }

                    sparrow_output_update_dpms_lifecycle();
                }

                wlr_output_state_finish(&out_state);
            }

            return;
        }

        if (sym == XKB_KEY_F9)
        {
            instance->buffering_mode = static_cast<Core::BufferingMode>(
                (static_cast<int>(instance->buffering_mode) + 1) % 3);
            const char *mode_str =
                (instance->buffering_mode == Core::BUFFERING_DOUBLE) ?
                "DOUBLE BUFFERING (DB)" :
                (instance->buffering_mode == Core::BUFFERING_AUTO) ?
                "DYNAMIC TRIPLE BUFFERING (AUTO)" :
                "FORCED TRIPLE BUFFERING (TB:ON)";
            wlr_log(WLR_INFO, "[BUFFERING] Mode changed via F9: %s", mode_str);
            sparrow_damage_add_box(nullptr);
            return;
        }

        if (sym == XKB_KEY_F10)
        {
            if (instance->trace_surfacetree)
            {
                instance->dump_surface_tree();
            }

            return;
        }

        if (sym == XKB_KEY_F11)
        {
            instance->show_fps = !instance->show_fps;
            instance->client_commit_count = 0;
            instance->client_commit_head  = 0;
            if (!instance->show_fps && instance->fps_decay_timer)
            {
                wl_event_source_timer_update(instance->fps_decay_timer, 0);
            }

            sparrow_damage_add_box(nullptr);
            return;
        }

        if (sym == XKB_KEY_F12)
        {
            instance->debug_damage = !instance->debug_damage;
            sparrow_damage_add_box(nullptr);
            return;
        }
    }

    uint32_t keycode =
        (message.keycode() >= 8) ? (message.keycode() - 8) : message.keycode();
#ifdef DEBUG
    if (instance->debug_key)
    {
        wlr_log(WLR_INFO,
            "[KEY] sparrow_handle_surface_keyboard_key: surface=%u, keycode=%u "
            "(evdev=%u), state=%d",
            (uint32_t)message.surface_handle(), (uint32_t)message.keycode(), keycode,
            (int)state);
    }

#endif

    wlr_seat_keyboard_notify_key(
        instance->seat, (uint32_t)(message.timestamp_micros() / 1000), keycode, state);
    if (instance->idle_notifier && instance->seat)
    {
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
    }
}

void sparrow_handle_surface_begin_move(uint32_t surface_handle)
{
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (view == nullptr)
    {
        return;
    }

    sparrow_view_focus(view);
    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }
}

static bool forward_pan_zoom_to_seat(Core *instance,
    const sparrow::SurfacePointerEventMessage & message, uint32_t time_msec)
{
    if (!instance || !instance->seat)
    {
        return false;
    }

    if (message.event_type() == pointerPanZoomStartEvent)
    {
        wlr_seat_pointer_notify_frame(instance->seat);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier, instance->seat);
        return true;
    }

    if (message.event_type() == pointerPanZoomUpdateEvent)
    {
        const bool is_pinch_gesture = (message.scale() != 1.0) ||
            (message.rotation() != 0.0) || instance->input.zoom_started;
        if (!is_pinch_gesture)
        {
            if (!instance->input.scrolls.empty())
            {
                for (const auto & cache : instance->input.scrolls)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, cache.time_msec, cache.orientation,
                        cache.delta, cache.delta_discrete, cache.source,
                        cache.relative_direction);
                    wlr_seat_pointer_notify_frame(instance->seat);
                }

                instance->input.scrolls.clear();
            } else
            {
                if (message.scroll_delta_y() != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                        -message.scroll_delta_y(), 0, WL_POINTER_AXIS_SOURCE_FINGER,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }

                if (message.scroll_delta_x() != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                        -message.scroll_delta_x(), 0, WL_POINTER_AXIS_SOURCE_FINGER,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }
            }

            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        }

        return true;
    }

    if (message.event_type() == pointerPanZoomEndEvent)
    {
        if (!instance->input.zoom_started)
        {
            wlr_seat_pointer_notify_axis(
                instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL, 0, 0,
                WL_POINTER_AXIS_SOURCE_FINGER,
                WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
            wlr_seat_pointer_notify_axis(
                instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL, 0, 0,
                WL_POINTER_AXIS_SOURCE_FINGER,
                WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        }

        return true;
    }

    return false;
}

void sparrow_handle_layer_surface_pointer_event(
    const sparrow::SurfacePointerEventMessage & message)
{
    const auto instance = Core::instance();
    if (!instance || !instance->seat)
    {
        return;
    }

    SparrowLayerSurface *layer_surf =
        sparrow_find_layer_surface_by_handle((uint32_t)message.surface_handle());
    if (!layer_surf || !layer_surf->layer_surface || !layer_surf->layer_surface->surface)
    {
        return;
    }

    struct wlr_surface *root_surface = layer_surf->layer_surface->surface;
    const uint32_t time_msec = (uint32_t)(message.timestamp() / 1000);

    double local_x = message.local_pos_x();
    double local_y = message.local_pos_y();

    if ((message.widget_size_x() > 0.0) && (message.widget_size_y() > 0.0))
    {
        const int vis_w = (layer_surf->width > 0) ? layer_surf->width : root_surface->current.width;
        const int vis_h = (layer_surf->height > 0) ? layer_surf->height : root_surface->current.height;
        if ((vis_w > 0) && (vis_h > 0))
        {
            const double sx = (double)vis_w / message.widget_size_x();
            const double sy = (double)vis_h / message.widget_size_y();
            if (isfinite(sx) && isfinite(sy) && (sx > 0.0) && (sy > 0.0))
            {
                local_x = message.local_pos_x() * sx;
                local_y = message.local_pos_y() * sy;
            }
        }
    }

    double sub_x = local_x, sub_y = local_y;
    struct wlr_surface *surface =
        wlr_layer_surface_v1_surface_at(layer_surf->layer_surface, local_x, local_y, &sub_x, &sub_y);
    if (surface != nullptr)
    {
        local_x = sub_x;
        local_y = sub_y;
    } else
    {
        surface = root_surface;
    }

    if ((message.event_type() == pointerEnterEvent) ||
        (message.event_type() == pointerHoverEvent) ||
        (message.event_type() == pointerMoveEvent))
    {
        if (instance->seat->pointer_state.focused_surface != surface)
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
        }

        wlr_seat_pointer_notify_motion(instance->seat, time_msec, local_x, local_y);
        wlr_seat_pointer_notify_frame(instance->seat);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier, instance->seat);
    } else if ((message.event_type() == pointerExitEvent) ||
               (message.event_type() == pointerCancelEvent))
    {
        if (instance->seat->pointer_state.focused_surface == surface)
        {
            wlr_seat_pointer_clear_focus(instance->seat);
            wlr_seat_pointer_notify_frame(instance->seat);
        }
    } else if ((message.event_type() == pointerDownEvent) ||
               (message.event_type() == pointerUpEvent))
    {
        if (layer_surf->layer_surface->current.keyboard_interactive !=
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
        {
            struct wlr_keyboard *kb = wlr_seat_get_keyboard(instance->seat);
            if (kb != nullptr)
            {
                wlr_seat_keyboard_notify_enter(
                    instance->seat, surface, kb->keycodes, kb->num_keycodes, &kb->modifiers);
            } else
            {
                wlr_seat_keyboard_notify_enter(
                    instance->seat, surface, nullptr, 0, nullptr);
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

        wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
        wlr_seat_pointer_notify_motion(instance->seat, time_msec, local_x, local_y);

        const int64_t prev_buttons = get_surface_buttons(
            message.surface_handle() + 300000);
        const int64_t next_buttons = message.buttons();
        const int64_t changed = prev_buttons ^ next_buttons;

        for (int bit = 0; bit < 16; bit++)
        {
            int64_t flutter_button = (1LL << bit);
            if ((changed & flutter_button) == 0)
            {
                continue;
            }

            uint32_t linux_button;
            if (!flutter_mouse_button_to_linux(flutter_button, &linux_button))
            {
                continue;
            }

            enum wl_pointer_button_state button_state =
                (next_buttons & flutter_button) ? WL_POINTER_BUTTON_STATE_PRESSED :
                WL_POINTER_BUTTON_STATE_RELEASED;

            wlr_seat_pointer_notify_button(instance->seat, time_msec, linux_button,
                button_state);
        }

        wlr_seat_pointer_notify_frame(instance->seat);
        set_surface_buttons(message.surface_handle() + 300000, next_buttons);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier, instance->seat);
    } else if (message.event_type() == pointerScrollEvent)
    {
        const double dx = message.scroll_delta_x();
        const double dy = message.scroll_delta_y();

        if (dx != 0.0)
        {
            wlr_seat_pointer_notify_axis(
                instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                dx, static_cast<int32_t>(std::round(dx)),
                WL_POINTER_AXIS_SOURCE_WHEEL,
                WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
        }

        if (dy != 0.0)
        {
            wlr_seat_pointer_notify_axis(
                instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                dy, static_cast<int32_t>(std::round(dy)),
                WL_POINTER_AXIS_SOURCE_WHEEL,
                WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
        }

        wlr_seat_pointer_notify_frame(instance->seat);
    } else if ((message.event_type() == pointerPanZoomStartEvent) ||
               (message.event_type() == pointerPanZoomUpdateEvent) ||
               (message.event_type() == pointerPanZoomEndEvent))
    {
        if (instance->seat->pointer_state.focused_surface != surface)
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
        }

        forward_pan_zoom_to_seat(instance, message, time_msec);
    }
}

void sparrow_handle_surface_begin_resize(uint32_t surface_handle,
    int64_t edges)
{
    (void)edges;
    const auto instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(surface_handle);
    if (view == nullptr)
    {
        return;
    }

    sparrow_view_focus(view);
    if (view->scene_tree != nullptr)
    {
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }
}
