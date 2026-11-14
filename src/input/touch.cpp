#include "core.hpp"
#include "flutter/platform/engine//messages/seat_message.hpp"
#include "output.hpp"
#include "pointer.hpp"
#include "seat.hpp"
#include "surface/session_lock.hpp"
#include <cstring>
#include <surface/view.hpp>

void on_server_cursor_touch_down(struct wl_listener *listener, void *data)
{
    const auto instance = Core::instance();
    struct wlr_touch_down_event *event =
        static_cast<wlr_touch_down_event*>(data);

    double lx = 0.0, ly = 0.0;
    wlr_cursor_absolute_to_layout_coords(instance->cursor, &event->touch->base,
        event->x, event->y, &lx, &ly);

#ifdef DEBUG
    if (instance->debug_touch)
    {
        wlr_log(WLR_INFO,
            "[TOUCH] touch_down: id=%d, raw=(%.2f, %.2f), layout=(%.2f, %.2f), time_msec=%u, locked=%d",
            event->touch_id, event->x, event->y, lx, ly, event->time_msec,
            sparrow_is_session_locked());
    }

#endif

    if (sparrow_is_session_locked())
    {
        double sx = 0.0, sy = 0.0;
        struct wlr_surface *surface =
            sparrow_session_lock_surface_at(lx, ly, &sx, &sy);
        if (surface != nullptr)
        {
            wlr_seat_touch_notify_down(instance->seat, surface, event->time_msec,
                event->touch_id, sx, sy);
            wlr_seat_touch_notify_frame(instance->seat);
            if (instance->seat->keyboard_state.focused_surface != surface)
            {
                sparrow_session_lock_focus_surface(surface);
            }
        }

        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
        return;
    }

    if (instance->engine == nullptr)
    {
        return;
    }

    if (instance->output_layout != nullptr)
    {
        struct wlr_output *wlr_out =
            wlr_output_layout_output_at(instance->output_layout, lx, ly);
        if (wlr_out != nullptr)
        {
            struct wlr_box box;
            wlr_output_layout_get_box(instance->output_layout, wlr_out, &box);
            if (ly >= (box.y + box.height - 30))
            {
                instance->touch_down_y = ly;
                instance->touch_in_bottom_edge = true;
            } else
            {
                instance->touch_in_bottom_edge = false;
            }
        }
    }

    auto point = seat_touch_point_add(event->touch_id);
    if (!point)
    {
        return;
    }

    point->event_mask = TouchEventkDown;
    point->sx = lx;
    point->sy = ly;
    point->time_msec = event->time_msec;

    // Direct touch mode - bypass Flutter for active surface (fullscreen & edge-to-edge)
    if (!instance->touch_in_bottom_edge)
    {
        struct wlr_surface *direct_surf = nullptr;
        double direct_sx = 0.0, direct_sy = 0.0;
        SparrowView *direct_view = sparrow_get_direct_input_view(
            instance, lx, ly, &direct_surf, &direct_sx, &direct_sy);

        if ((direct_view != nullptr) && (direct_surf != nullptr))
        {
            sparrow_update_direct_input_mode(instance, true, direct_view->handle, "touch_down");
            point->is_direct = true;

            wlr_seat_touch_notify_down(instance->seat, direct_surf, event->time_msec,
                event->touch_id, direct_sx, direct_sy);
            wlr_seat_touch_notify_frame(instance->seat);
            if (instance->seat->keyboard_state.focused_surface != direct_surf)
            {
                sparrow_view_focus(direct_view);
            }

            wlr_idle_notifier_v1_notify_activity(instance->idle_notifier, instance->seat);
            return;
        }
    }

    point->is_direct = false;
    sparrow_update_direct_input_mode(instance, false, 0, "touch_down_flutter");
    send_flutter_input_event(
        kDown, kFlutterPointerDeviceKindTouch, kFlutterPointerSignalKindNone, lx,
        ly, event->touch_id + 1, seat_get_flutter_timestamp(event->time_msec));
}

void on_server_cursor_touch_up(struct wl_listener *listener, void *data)
{
    const auto instance = Core::instance();
    instance->touch_in_bottom_edge = false;
    const struct wlr_touch_up_event *event =
        static_cast<wlr_touch_up_event*>(data);

#ifdef DEBUG
    if (instance->debug_touch)
    {
        wlr_log(WLR_INFO,
            "[TOUCH] touch_up: id=%d, time_msec=%u, locked=%d",
            event->touch_id, event->time_msec, sparrow_is_session_locked());
    }

#endif

    if (sparrow_is_session_locked())
    {
        wlr_seat_touch_notify_up(instance->seat, event->time_msec, event->touch_id);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
        return;
    }

    if (instance->engine == nullptr)
    {
        return;
    }

    auto point = seat_touch_point_get(event->touch_id);
    if (!point)
    {
        return;
    }

    if (point->is_direct)
    {
        wlr_seat_touch_notify_up(instance->seat, event->time_msec, event->touch_id);
        wlr_seat_touch_notify_frame(instance->seat);
        seat_touch_point_delete(event->touch_id);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier, instance->seat);
        return;
    }

    if ((point->event_mask != TouchEventkDown) &&
        (point->event_mask != TouchEventkMotion) &&
        (point->event_mask != TouchEventkCancel))
    {
        return;
    }

    point->event_mask = TouchEventkUp;
    point->time_msec  = event->time_msec;

    send_flutter_input_event(kUp, kFlutterPointerDeviceKindTouch,
        kFlutterPointerSignalKindNone, point->sx, point->sy,
        event->touch_id + 1,
        seat_get_flutter_timestamp(event->time_msec));
}

void on_server_cursor_touch_motion(struct wl_listener *listener, void *data)
{
    const auto instance = Core::instance();
    struct wlr_touch_motion_event *event =
        static_cast<wlr_touch_motion_event*>(data);

    double lx = 0.0, ly = 0.0;
    wlr_cursor_absolute_to_layout_coords(instance->cursor, &event->touch->base,
        event->x, event->y, &lx, &ly);

#ifdef DEBUG
    if (instance->debug_touch)
    {
        wlr_log(WLR_INFO,
            "[TOUCH] touch_motion: id=%d, raw=(%.2f, %.2f), layout=(%.2f, %.2f), time_msec=%u, locked=%d",
            event->touch_id, event->x, event->y, lx, ly, event->time_msec,
            sparrow_is_session_locked());
    }

#endif

    if (sparrow_is_session_locked())
    {
        double sx = 0.0, sy = 0.0;
        struct wlr_surface *surface =
            sparrow_session_lock_surface_at(lx, ly, &sx, &sy);
        if (surface != nullptr)
        {
            wlr_seat_touch_notify_motion(instance->seat, event->time_msec,
                event->touch_id, sx, sy);
        }

        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
        return;
    }

    if (instance->engine == nullptr)
    {
        return;
    }

    if (instance->touch_in_bottom_edge && instance->immersive_sticky)
    {
        double delta_y = instance->touch_down_y - ly;
        if (delta_y > 20.0)
        {
            bool has_views = !wl_list_empty(&instance->views_list) &&
                !instance->force_render_all_views;
            if (has_views && instance->pigeon_flutter_api)
            {
                wlr_log(WLR_INFO,
                    "[DOCK] Touch swipe-up at bottom edge, revealing dock");
                instance->immersive_sticky     = false;
                instance->touch_in_bottom_edge = false;
                instance->sparrow_renderer.current_scene.needs_update = true;
                instance->pigeon_flutter_api->DockReveal(
                    [] () {}, [] (const sparrow::FlutterError &) {});
                sparrow_damage_add_box(nullptr);
            }
        }
    }

    auto point = seat_touch_point_get(event->touch_id);
    if (!point)
    {
        return;
    }

    if (point->is_direct)
    {
        struct wlr_surface *direct_surf = nullptr;
        double direct_sx = 0.0, direct_sy = 0.0;
        SparrowView *direct_view = sparrow_get_direct_input_view(
            instance, lx, ly, &direct_surf, &direct_sx, &direct_sy);
        (void)direct_view;
        if (direct_surf != nullptr)
        {
            wlr_seat_touch_notify_motion(instance->seat, event->time_msec,
                event->touch_id, direct_sx, direct_sy);
            wlr_seat_touch_notify_frame(instance->seat);
        }

        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier, instance->seat);
        return;
    }

    if ((point->event_mask != TouchEventkDown) &&
        (point->event_mask != TouchEventkMotion) &&
        (point->event_mask != TouchEventkUp))
    {
        return;
    }

    point->event_mask = TouchEventkMotion;
    point->sx = lx;
    point->sy = ly;
    point->time_msec = event->time_msec;

    send_flutter_input_event(
        kMove, kFlutterPointerDeviceKindTouch, kFlutterPointerSignalKindNone, lx,
        ly, event->touch_id + 1, seat_get_flutter_timestamp(event->time_msec));
}

void on_server_cursor_touch_cancel(struct wl_listener *listener, void *data)
{
    const auto instance = Core::instance();
    instance->touch_in_bottom_edge = false;
    const struct wlr_touch_cancel_event *event =
        static_cast<wlr_touch_cancel_event*>(data);

#ifdef DEBUG
    if (instance->debug_touch)
    {
        wlr_log(WLR_INFO,
            "[TOUCH] touch_cancel: id=%d, time_msec=%u, locked=%d",
            event->touch_id, event->time_msec, sparrow_is_session_locked());
    }

#endif

    if (sparrow_is_session_locked())
    {
        struct wlr_touch_point *point =
            wlr_seat_touch_get_point(instance->seat, event->touch_id);
        if (point && point->client)
        {
            wlr_seat_touch_notify_cancel(instance->seat, point->client);
        }

        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
        return;
    }

    if (instance->engine == nullptr)
    {
        return;
    }

    auto point = seat_touch_point_get(event->touch_id);
    if (!point)
    {
        return;
    }

    if (point->is_direct)
    {
        struct wlr_touch_point *wlr_point =
            wlr_seat_touch_get_point(instance->seat, event->touch_id);
        if (wlr_point && wlr_point->client)
        {
            wlr_seat_touch_notify_cancel(instance->seat, wlr_point->client);
        }

        wlr_seat_touch_notify_frame(instance->seat);
        seat_touch_point_delete(event->touch_id);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier, instance->seat);
        return;
    }

    if ((point->event_mask != TouchEventkDown) &&
        (point->event_mask != TouchEventkMotion) &&
        (point->event_mask != TouchEventkUp))
    {
        return;
    }

    point->event_mask = TouchEventkCancel;

    send_flutter_input_event(kCancel, kFlutterPointerDeviceKindTouch,
        kFlutterPointerSignalKindNone, point->sx, point->sy,
        event->touch_id + 1,
        seat_get_flutter_timestamp(event->time_msec));
}

void on_server_cursor_touch_frame(struct wl_listener *listener, void *data)
{
    (void)listener;
    (void)data;
    const auto instance = Core::instance();

#ifdef DEBUG
    if (instance && instance->debug_touch)
    {
        wlr_log(WLR_INFO, "[TOUCH] touch_frame");
    }

#endif

    if (instance && instance->seat)
    {
        wlr_seat_touch_notify_frame(instance->seat);
        wlr_idle_notifier_v1_notify_activity(instance->idle_notifier,
            instance->seat);
    }
}

static void touch_destroy(struct wl_listener *listener, void *data)
{
    struct sparrow_touch *touch = wl_container_of(listener, touch, destroy);
    wl_list_remove(&touch->destroy.link);
    wl_list_remove(&touch->link);
    delete touch;

    sparrow_seat_update_capabilities();
}

void map_touch_to_output(struct wlr_input_device *&device, Output *output)
{
    const auto instance = Core::instance();

    if (output)
    {
        wlr_log(WLR_INFO, "Mapping input %s to output %s.", device->name,
            output->wlr_output->name);
        wlr_cursor_map_input_to_output(instance->cursor, device,
            output->wlr_output);
    } else
    {
        wlr_log(WLR_INFO, "Mapping input %s  to output null.", device->name);
        wlr_cursor_map_input_to_output(instance->cursor, device, nullptr);
    }
}

void map_touch_to_output(struct wlr_input_device *&device)
{
    const auto instance = Core::instance();
    struct wlr_touch *wlr_touch = wlr_touch_from_input_device(device);

    // wlr_log(WLR_INFO, "wlr_touch->output_name %s", wlr_touch->output_name);
    Output *output = nullptr;
    Output *o = nullptr;

    wl_list_for_each(o, &instance->outputs, link)
    {
        if (!output)
        {
            output = o;
        }

        if (wlr_touch->output_name && o->wlr_output->name &&
            (strcmp(o->wlr_output->name, wlr_touch->output_name) == 0))
        {
            output = o;
            break;
        }
    }

    struct sparrow_touch *touch = new sparrow_touch();
    if (!touch)
    {
        wlr_log(WLR_ERROR, "Failed to allocate sparrow_touch");
        exit(1);
    }

    touch->instance  = instance;
    touch->device    = device;
    touch->wlr_touch = wlr_touch;

    touch->destroy.notify = touch_destroy;
    wl_signal_add(&wlr_touch->base.events.destroy, &touch->destroy);

    wl_list_insert(&instance->touchs, &touch->link);

    map_touch_to_output(device, output);
}

void server_new_touch(struct wlr_input_device *device)
{
    const auto instance = Core::instance();

    wlr_cursor_attach_input_device(instance->cursor, device);

    map_touch_to_output(device);

    // wlr_touch_init(wlr_touch, &touch_impl, "stipc_touch");
    seat_configure_libinput_device(device);

    if (!wlr_input_device_is_libinput(device))
    {
        wlr_log(WLR_DEBUG, "Touch device is not a libinput device. (%s) (%i)",
            device->name, device->type);
        return;
    }

    auto libinput_dev = wlr_libinput_get_device_handle(device);
    float m[6];

    libinput_device_config_calibration_get_default_matrix(libinput_dev, m);
    libinput_device_config_calibration_set_matrix(libinput_dev, m);
}
