#include <core.hpp>
#include <flutter/platform/cursor.hpp>
#include <flutter/platform/messages.hpp>
#include <input/pointer.hpp>
#include <input/seat.hpp>
#include <surface/popup.hpp>
#include <surface/view.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

void sparrow_handle_surface_pointer_event(const surface_pointer_event_message& message)
{
    Core *instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(message.surface_handle);
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
    double parent_x = message.local_pos_x;
    double parent_y = message.local_pos_y;

    if ((message.widget_size_x > 0.0) && (message.widget_size_y > 0.0))
    {
        const int vis_w =
            (view->width > 0) ? view->width : parent_surface->current.width;
        const int vis_h =
            (view->height > 0) ? view->height : parent_surface->current.height;
        if ((vis_w > 0) && (vis_h > 0))
        {
            const double sx = (double)vis_w / message.widget_size_x;
            const double sy = (double)vis_h / message.widget_size_y;
            if (isfinite(sx) && isfinite(sy) && (sx > 0.0) && (sy > 0.0))
            {
                parent_x = (double)view->geo_x + (message.local_pos_x * sx);
                parent_y = (double)view->geo_y + (message.local_pos_y * sy);
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

    uint32_t time_msec = (uint32_t)(message.timestamp / 1000);

    // Track Flutter cursor position for grab operations
    instance->input.flutter_cursor_x = message.global_pos_x;
    instance->input.flutter_cursor_y = message.global_pos_y;

    if (PointerDeviceKind::touch == to_device_kind(message.kind))
    {
        const int32_t touch_id = (int32_t)(message.device - 1);
        auto point = seat_touch_point_get(touch_id);
        const uint32_t touch_time_msec = point ? point->time_msec : time_msec;

        if (message.event_type == pointerMoveEvent)
        {
            if (surface)
            {
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
        } else if (message.event_type == pointerDownEvent)
        {
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
        } else if (message.event_type == pointerUpEvent)
        {
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
        } else if (message.event_type == pointerCancelEvent)
        {
            wlr_seat_touch_point_clear_focus(instance->seat, touch_time_msec,
                touch_id);
            wlr_seat_touch_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
            seat_touch_point_delete(touch_id);
        }
    } else
    {
        if (message.event_type == pointerEnterEvent)
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
            wlr_seat_pointer_notify_frame(instance->seat);
            sparrow_pointer_constraints_set_focus(instance, surface);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type == pointerExitEvent)
        {
            sparrow_pointer_constraints_deactivate(instance);
            wlr_seat_pointer_clear_focus(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
            sparrow_cursor_reset_to_flutter();

            // Don't clear button state on exit - track by surface handle instead
        } else if ((message.event_type == pointerHoverEvent) ||
                   (message.event_type == pointerMoveEvent))
        {
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
                wlr_seat_pointer_notify_motion(instance->seat, time_msec,
                    local_x, local_y);
                wlr_seat_pointer_notify_frame(instance->seat);
                wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                    instance->seat);
            }

            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if ((message.event_type == pointerDownEvent) ||
                   (message.event_type == pointerUpEvent))
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
            sparrow_view_focus(view);
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
            wlr_log(WLR_DEBUG, "message.%s",
                message.event_type == pointerDownEvent ? "pointerDownEvent" :
                "pointerUpEvent");

            // Track button state by surface handle (more reliable than pointer ID
            // which can change)
            const int64_t prev_buttons = get_surface_buttons(message.surface_handle);
            const int64_t next_buttons = message.buttons;
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

                wlr_seat_pointer_notify_button(instance->seat, button_time,
                    linux_button, state);
            }

            set_surface_buttons(message.surface_handle, next_buttons);
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type == pointerScrollEvent)
        {
            // Flutter-first scroll: Flutter receives scroll from embedder, does
            // hit-testing, and forwards to the correct surface via this platform
            // channel handler. NOTE: Don't call notify_enter here - seat already has
            // focus from motion events.
            if (!instance->input.scrolls.empty())
            {
                for (const auto & cache : instance->input.scrolls)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, cache.time_msec, cache.orientation, cache.delta,
                        cache.delta_discrete, cache.source, cache.relative_direction);
                    wlr_seat_pointer_notify_frame(instance->seat);
                }

                instance->input.scrolls.clear();
            } else
            {
                if (message.scroll_delta_y != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                        -message.scroll_delta_y, 0, WL_POINTER_AXIS_SOURCE_WHEEL,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }

                if (message.scroll_delta_x != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                        -message.scroll_delta_x, 0, WL_POINTER_AXIS_SOURCE_WHEEL,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }
            }

            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type == pointerPanZoomStartEvent)
        {
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type == pointerPanZoomUpdateEvent)
        {
            const bool is_pinch_gesture = (message.scale != 1.0) ||
                (message.rotation != 0.0) ||
                instance->input.zoom_started;

            if (!is_pinch_gesture)
            {
                if (!instance->input.scrolls.empty())
                {
                    for (const auto & cache : instance->input.scrolls)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, cache.time_msec, cache.orientation, cache.delta,
                            cache.delta_discrete, cache.source, cache.relative_direction);
                        wlr_seat_pointer_notify_frame(instance->seat);
                    }

                    instance->input.scrolls.clear();
                } else
                {
                    if (message.scroll_delta_y != 0.0)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                            -message.scroll_delta_y, 0, WL_POINTER_AXIS_SOURCE_FINGER,
                            WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                    }

                    if (message.scroll_delta_x != 0.0)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                            -message.scroll_delta_x, 0, WL_POINTER_AXIS_SOURCE_FINGER,
                            WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                    }
                }

                wlr_seat_pointer_notify_frame(instance->seat);
                wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                    instance->seat);
            }
        } else if (message.event_type == pointerPanZoomEndEvent)
        {
            const bool is_pinch_gesture = instance->input.zoom_started;
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

void sparrow_handle_popup_pointer_event(const surface_pointer_event_message& message)
{
    Core *instance = Core::instance();

    wlr_log(WLR_DEBUG, "popup_pointer_event: decoded handle=%d type=%d",
        message.surface_handle, message.event_type);

    SparrowPopup *popup;
    if (!handle_map_get(instance->popups, message.surface_handle,
        (void**)&popup) ||
        (popup == nullptr))
    {
        wlr_log(WLR_DEBUG, "popup_pointer_event: popup handle %d not found",
            message.surface_handle);
        return;
    }

    struct wlr_surface *popup_surface = popup->xdg_surface->surface;
    if (popup_surface == nullptr)
    {
        return;
    }

    const uint32_t time_msec = (uint32_t)(message.timestamp / 1000);

    // Find the actual surface under cursor - Firefox renders content to
    // subsurfaces
    double local_x = message.local_pos_x;
    double local_y = message.local_pos_y;

    if ((message.widget_size_x > 0.0) && (message.widget_size_y > 0.0))
    {
        const int vis_w =
            (popup->width > 0) ? popup->width : popup_surface->current.width;
        const int vis_h =
            (popup->height > 0) ? popup->height : popup_surface->current.height;
        if ((vis_w > 0) && (vis_h > 0))
        {
            const double sx = (double)vis_w / message.widget_size_x;
            const double sy = (double)vis_h / message.widget_size_y;
            if (isfinite(sx) && isfinite(sy) && (sx > 0.0) && (sy > 0.0))
            {
                local_x = message.local_pos_x * sx;
                local_y = message.local_pos_y * sy;
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

    wlr_log(
        WLR_DEBUG,
        "Popup %d pointer event type=%d at (%.1f,%.1f) -> surface=%p (%.1f,%.1f)",
        message.surface_handle, message.event_type, message.local_pos_x,
        message.local_pos_y, static_cast<void*>(surface), local_x, local_y);

    if (PointerDeviceKind::touch == to_device_kind(message.kind))
    {
        const int32_t touch_id = (int32_t)(message.device - 1);
        auto point = seat_touch_point_get(touch_id);
        const uint32_t touch_time_msec = point ? point->time_msec : time_msec;

        if (message.event_type == pointerMoveEvent)
        {
            if (surface)
            {
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
        } else if (message.event_type == pointerDownEvent)
        {
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
        } else if (message.event_type == pointerUpEvent)
        {
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
        } else if (message.event_type == pointerCancelEvent)
        {
            wlr_seat_touch_point_clear_focus(instance->seat, touch_time_msec,
                touch_id);
            wlr_seat_touch_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
            seat_touch_point_delete(touch_id);
        }
    } else
    {
        if (message.event_type == pointerEnterEvent)
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type == pointerExitEvent)
        {
            // Don't clear focus on popup exit - let the parent or next popup handle
            // it
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if ((message.event_type == pointerHoverEvent) ||
                   (message.event_type == pointerMoveEvent))
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
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
        } else if ((message.event_type == pointerDownEvent) ||
                   (message.event_type == pointerUpEvent))
        {
            wlr_seat_pointer_notify_enter(instance->seat, surface, local_x, local_y);
            sparrow_focus_popup(popup);
            if (popup->scene_tree != nullptr)
            {
                wlr_scene_node_raise_to_top(&popup->scene_tree->node);
            }

            // Track button state by popup handle
            const int64_t prev_buttons = get_surface_buttons(
                message.surface_handle +
                200000); // Offset to avoid collision with surface handles
            const int64_t next_buttons = message.buttons;
            const int64_t changed = prev_buttons ^ next_buttons;

            // Use cached button timestamp for accurate Wayland delivery
            const uint32_t button_time = instance->input.last_button.valid ?
                instance->input.last_button.time_msec :
                time_msec;
            instance->input.last_button.valid = false;

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
                wlr_seat_pointer_notify_button(instance->seat, button_time,
                    linux_button, state);
            }

            set_surface_buttons(message.surface_handle + 200000, next_buttons);
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type == pointerScrollEvent)
        {
            // Flutter-first scroll: Flutter receives scroll from embedder, does
            // hit-testing, and forwards to the correct popup via this platform
            // channel handler. NOTE: Don't call notify_enter here - seat already has
            // focus from motion events.
            if (!instance->input.scrolls.empty())
            {
                for (const auto & cache : instance->input.scrolls)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, cache.time_msec, cache.orientation, cache.delta,
                        cache.delta_discrete, cache.source, cache.relative_direction);
                    wlr_seat_pointer_notify_frame(instance->seat);
                }

                instance->input.scrolls.clear();
            } else
            {
                if (message.scroll_delta_y != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                        -message.scroll_delta_y, 0, WL_POINTER_AXIS_SOURCE_WHEEL,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }

                if (message.scroll_delta_x != 0.0)
                {
                    wlr_seat_pointer_notify_axis(
                        instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                        -message.scroll_delta_x, 0, WL_POINTER_AXIS_SOURCE_WHEEL,
                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                }
            }

            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type == pointerPanZoomStartEvent)
        {
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
        } else if (message.event_type == pointerPanZoomUpdateEvent)
        {
            const bool is_pinch_gesture = (message.scale != 1.0) ||
                (message.rotation != 0.0) ||
                instance->input.zoom_started;

            if (!is_pinch_gesture)
            {
                if (!instance->input.scrolls.empty())
                {
                    for (const auto & cache : instance->input.scrolls)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, cache.time_msec, cache.orientation, cache.delta,
                            cache.delta_discrete, cache.source, cache.relative_direction);
                        wlr_seat_pointer_notify_frame(instance->seat);
                    }

                    instance->input.scrolls.clear();
                } else
                {
                    if (message.scroll_delta_y != 0.0)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
                            -message.scroll_delta_y, 0, WL_POINTER_AXIS_SOURCE_FINGER,
                            WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                    }

                    if (message.scroll_delta_x != 0.0)
                    {
                        wlr_seat_pointer_notify_axis(
                            instance->seat, time_msec, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                            -message.scroll_delta_x, 0, WL_POINTER_AXIS_SOURCE_FINGER,
                            WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
                    }
                }

                wlr_seat_pointer_notify_frame(instance->seat);
                wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                    instance->seat);
            }
        } else if (message.event_type == pointerPanZoomEndEvent)
        {
            const bool is_pinch_gesture = instance->input.zoom_started;
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

void sparrow_handle_surface_keyboard_key(const surface_keyboard_key_message& message)
{
    Core *instance = Core::instance();

    SparrowView *view = instance->find_view_by_handle(message.surface_handle);
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

    enum wl_keyboard_key_state state = message.event_type != 0 ?
        WL_KEYBOARD_KEY_STATE_PRESSED :
        WL_KEYBOARD_KEY_STATE_RELEASED;

    if ((state == WL_KEYBOARD_KEY_STATE_PRESSED) && keyboard && keyboard->xkb_state)
    {
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(keyboard->xkb_state,
            (xkb_keycode_t)message.keycode);
        if (sym == XKB_KEY_F8)
        {
            Output *out = instance->vsync_output ? instance->vsync_output : sparrow_get_first_output();
            if (out && out->wlr_output)
            {
                struct wlr_output_state out_state;
                wlr_output_state_init(&out_state);
                bool new_enabled = !out->wlr_output->enabled;
                wlr_output_state_set_enabled(&out_state, new_enabled);
                if (!wlr_output_commit_state(out->wlr_output, &out_state))
                {
                    wlr_log(WLR_ERROR, "[DPMS] Failed to commit power toggle for output '%s'",
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
                }

                wlr_output_state_finish(&out_state);
            }

            return;
        }

        if (sym == XKB_KEY_F9)
        {
            instance->buffering_mode =
                static_cast<Core::BufferingMode>((static_cast<int>(instance->buffering_mode) + 1) % 3);
            const char *mode_str = (instance->buffering_mode ==
                Core::BUFFERING_DOUBLE) ? "DOUBLE BUFFERING (DB)" :
                (instance->buffering_mode == Core::BUFFERING_AUTO) ? "DYNAMIC TRIPLE BUFFERING (AUTO)" :
                "FORCED TRIPLE BUFFERING (TB:ON)";
            wlr_log(WLR_INFO, "[BUFFERING] Mode changed via F9: %s", mode_str);
            sparrow_damage_add_box(nullptr);
            return;
        }

        if (sym == XKB_KEY_F10)
        {
            instance->dump_surface_tree();
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

    wlr_seat_keyboard_notify_key(instance->seat,
        (uint32_t)(message.timestamp / 1000),
        (uint32_t)message.keycode, state);
}

void sparrow_handle_surface_begin_move(uint32_t surface_handle)
{
    Core *instance = Core::instance();

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

void sparrow_handle_surface_begin_resize(uint32_t surface_handle, int64_t edges)
{
    (void)edges;
    Core *instance = Core::instance();

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
