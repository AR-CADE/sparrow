#include "pointer.hpp"
#include "flutter/platform/cursor.hpp"
#include "flutter/platform/engine/messages/seat_message.hpp"
#include "seat.hpp"
#include <core.hpp>
#include <surface/view.hpp>

void process_cursor_motion(uint32_t time, double dx, double dy,
    double dx_unaccel, double dy_unaccel,
    bool is_trackpad)
{
    Core *instance = Core::instance();

    // Direct input mode - bypass Flutter for low-latency gaming
    if (instance->direct_input_mode && (instance->direct_input_surface != 0))
    {
        SparrowView *view =
            instance->find_view_by_handle(instance->direct_input_surface);
        if (view != nullptr)
        {
            struct wlr_surface *surface = view->xdg_surface->surface;
            // Calculate surface-local coordinates
            // const int titlebar_height = view->uses_ssd ? 38 : 0;
            const int titlebar_height = 0;
            const double sx = instance->cursor->x - view->x;
            const double sy = instance->cursor->y - view->y - titlebar_height;
            wlr_seat_pointer_notify_enter(instance->seat, surface, sx, sy);
            wlr_seat_pointer_notify_motion(instance->seat, time, sx, sy);
            wlr_seat_pointer_notify_frame(instance->seat);
            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
                instance->seat);
            return;
        }
    }

    // Flutter-first: Send all motion events to Flutter
    // Flutter's widget tree does hit testing and forwards to surfaces as needed
    FlutterPointerPhase phase =
        instance->input.fl_mouse_button_mask != 0 || is_trackpad ? kMove : kHover;

    auto timestamp = seat_get_flutter_timestamp(time);

    send_flutter_mouse_event(phase, kFlutterPointerSignalKindNone, dx, dy,
        timestamp);

    // Add damage for old cursor position and new cursor position
    // Cursor hotspot can vary from (0,0) to (32,32) and cursor sizes up to 48x48.
    // Box of 80x80 offset by -32 ensures 100% of the previous cursor is cleanly erased
    // across all frame buffers without leaving ghost trails.
    static double s_prev_cx = -1.0, s_prev_cy = -1.0;
    if ((s_prev_cx >= 0.0) && (s_prev_cy >= 0.0))
    {
        struct wlr_box old_cursor_box = {
            .x     = (int)s_prev_cx - 32,
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
    Core *instance = Core::instance();
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

    if (core->active_constraint && (core->active_constraint->surface == surface))
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
    Core *instance = Core::instance();
    struct wlr_pointer_constraint_v1 *wlr_constraint =
        static_cast<wlr_pointer_constraint_v1*>(data);

    struct sparrow_pointer_constraint *constraint = new sparrow_pointer_constraint();
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
    Core *instance = Core::instance();

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
    Core *instance = Core::instance();
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

    if (instance->active_constraint &&
        (instance->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED))
    {
        sparrow_pointer_constraint_confine(instance->active_constraint);
    }

    process_cursor_motion(event->time_msec, event->delta_x,
        event->delta_y, event->unaccel_dx, event->unaccel_dy);
}

void on_server_cursor_motion_absolute(struct wl_listener *listener,
    void *data)
{
    (void)listener;
    Core *instance = Core::instance();
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

    if (instance->active_constraint &&
        (instance->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED))
    {
        sparrow_pointer_constraint_confine(instance->active_constraint);
    }

    process_cursor_motion(event->time_msec, dx, dy, dx, dy);
}

void on_server_cursor_button(struct wl_listener *listener, void *data)
{
    (void)listener;
    Core *instance = Core::instance();
    struct wlr_pointer_button_event *event =
        static_cast<wlr_pointer_button_event*>(data);

    int64_t flutter_button_mask =
        seat_flutter_button_mask_from_linux(event->button);
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        instance->input.fl_mouse_button_mask |= flutter_button_mask;
    } else
    {
        instance->input.fl_mouse_button_mask &= ~flutter_button_mask;
    }

    // Pointer Lock mode - bypass Flutter for low-latency gaming / locked pointer
    if (instance->active_constraint &&
        (instance->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED))
    {
        wlr_seat_pointer_notify_button(instance->seat, event->time_msec,
            event->button, event->state);
        wlr_seat_pointer_notify_frame(instance->seat);
        return;
    }

    // Direct input mode - bypass Flutter for low-latency gaming
    if (instance->direct_input_mode && (instance->direct_input_surface != 0))
    {
        const SparrowView *view =
            instance->find_view_by_handle(instance->direct_input_surface);
        if (view != nullptr)
        {
            wlr_seat_pointer_notify_button(instance->seat, event->time_msec,
                event->button, event->state);
            wlr_seat_pointer_notify_frame(instance->seat);
            return;
        }
    }

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
    Core *instance = Core::instance();
    struct wlr_pointer_axis_event *event =
        static_cast<wlr_pointer_axis_event*>(data);

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

    // Direct input mode - bypass Flutter for low-latency gaming
    if (instance->direct_input_mode && (instance->direct_input_surface != 0))
    {
        SparrowView *view =
            instance->find_view_by_handle(instance->direct_input_surface);
        if (view != nullptr)
        {
            wlr_seat_pointer_notify_axis(
                instance->seat, event->time_msec, event->orientation, event->delta,
                event->delta_discrete, event->source, event->relative_direction);
            wlr_seat_pointer_notify_frame(instance->seat);
            return;
        }
    }

    const bool is_touchpad = (event->source == WL_POINTER_AXIS_SOURCE_FINGER) ||
        (event->source == WL_POINTER_AXIS_SOURCE_CONTINUOUS);

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
        if (event->delta == 0.0)
        {
            if (instance->input.pan_started)
            {
                send_flutter_trackpad_event(kPanZoomEnd, timestamp,
                    instance->input.pan_x,
                    instance->input.pan_y, 1.0, 0.0);
                instance->input.pan_started = false;
            }
        } else
        {
            if (!instance->input.pan_started)
            {
                instance->input.pan_x = 0.0;
                instance->input.pan_y = 0.0;
                send_flutter_trackpad_event(kPanZoomStart, timestamp, 0.0, 0.0, 1.0,
                    0.0);
                instance->input.pan_started = true;
            }

            instance->input.pan_x += scroll_delta_x;
            instance->input.pan_y += scroll_delta_y;

            send_flutter_trackpad_event(kPanZoomUpdate, timestamp,
                instance->input.pan_x, instance->input.pan_y,
                1.0, 0.0);
        }
    } else
    {
        send_flutter_mouse_event(kMove, kFlutterPointerSignalKindScroll,
            scroll_delta_x, scroll_delta_y, timestamp);
    }
}

void on_server_cursor_frame(struct wl_listener *listener, void *data)
{
    (void)listener;
    (void)data;
}

void handle_swipe_begin(struct wl_listener *listener, void *data)
{
    (void)listener;
    Core *core = Core::instance();
    struct wlr_pointer_swipe_begin_event *event =
        static_cast<wlr_pointer_swipe_begin_event*>(data);

    wlr_log(WLR_INFO, "swipe begin, %u, fingers=%u", event->time_msec,
        event->fingers);

    if (event->fingers >= 3)
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

void handle_swipe_update(struct wl_listener *listener, void *data)
{
    (void)listener;
    Core *core = Core::instance();
    struct wlr_pointer_swipe_update_event *event =
        static_cast<wlr_pointer_swipe_update_event*>(data);

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
    Core *core = Core::instance();
    struct wlr_pointer_swipe_end_event *event =
        static_cast<wlr_pointer_swipe_end_event*>(data);

    wlr_log(WLR_INFO, "swipe end, %u", event->time_msec);

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
    Core *core = Core::instance();
    struct wlr_pointer_pinch_begin_event *event =
        static_cast<wlr_pointer_pinch_begin_event*>(data);

    wlr_log(WLR_INFO, "pinch begin, %u, fingers=%u", event->time_msec,
        event->fingers);

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
    Core *core = Core::instance();
    struct wlr_pointer_pinch_update_event *event =
        static_cast<wlr_pointer_pinch_update_event*>(data);

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
    Core *core = Core::instance();
    struct wlr_pointer_pinch_end_event *event =
        static_cast<wlr_pointer_pinch_end_event*>(data);

    wlr_log(WLR_INFO, "pinch end, %u", event->time_msec);

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
    Core *core = Core::instance();
    struct wlr_pointer_hold_begin_event *event =
        static_cast<wlr_pointer_hold_begin_event*>(data);

    if (core->pointer_gestures)
    {
        wlr_pointer_gestures_v1_send_hold_begin(core->pointer_gestures, core->seat,
            event->time_msec, event->fingers);
    }
}

void handle_hold_end(struct wl_listener *listener, void *data)
{
    (void)listener;
    Core *core = Core::instance();
    struct wlr_pointer_hold_end_event *event =
        static_cast<wlr_pointer_hold_end_event*>(data);

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
    Core *instance = Core::instance();
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
            instance->client_cursor_destroy.notify = client_cursor_handle_destroy;
            wl_signal_add(&event->surface->events.destroy,
                &instance->client_cursor_destroy);
            // Clear xcursor name when using client surface
            instance->current_xcursor_name.clear();
            wlr_cursor_set_surface(instance->cursor, event->surface, event->hotspot_x,
                event->hotspot_y);
        } else
        {
            instance->current_xcursor_name = "left_ptr";
            if (instance->cursor && instance->cursor_mgr)
            {
                wlr_cursor_set_xcursor(instance->cursor, instance->cursor_mgr,
                    "left_ptr");
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
    Core *instance = Core::instance();
    auto *event    =
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
                if (output && output->wlr_output && output->wlr_output->enabled &&
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
    Core *instance = Core::instance();

    wl_list_remove(&pointer->link);
    wlr_cursor_detach_input_device(instance->cursor, &pointer->pointer->base);
    wl_list_remove(&pointer->destroy.link);
    delete pointer;

    sparrow_seat_update_capabilities();
}

void server_new_pointer(struct wlr_input_device *device)
{
    Core *instance = Core::instance();

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
    Core *instance = Core::instance();
    struct wlr_virtual_pointer_v1_new_pointer_event *event =
        static_cast<struct wlr_virtual_pointer_v1_new_pointer_event*>(data);
    struct wlr_virtual_pointer_v1 *virtual_pointer = event->new_pointer;

    wlr_cursor_attach_input_device(instance->cursor,
        &virtual_pointer->pointer.base);

    struct sparrow_pointer *pointer = new sparrow_pointer();
    if (!pointer)
    {
        wlr_log(WLR_ERROR, "Failed to allocate sparrow_pointer for virtual pointer");
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
