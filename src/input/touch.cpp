#include "core.hpp"
#include "output.hpp"
#include "seat.hpp"
#include "flutter/platform/engine//messages/seat_message.hpp"
#include <cstring>

void on_server_cursor_touch_down(struct wl_listener *listener,
    void *data)
{
    const Core *instance = Core::instance();
    struct wlr_touch_down_event *event =
        static_cast<wlr_touch_down_event*>(data);

    if (instance->engine == nullptr)
    {
        return;
    }

    double lx = 0.0, ly = 0.0;
    wlr_cursor_absolute_to_layout_coords(instance->cursor, &event->touch->base,
        event->x, event->y, &lx, &ly);

    auto point = seat_touch_point_add(event->touch_id);
    if (!point)
    {
        return;
    }

    point->event_mask = TouchEventkDown;
    point->sx = lx;
    point->sy = ly;
    point->time_msec = event->time_msec;

    send_flutter_input_event(kDown, kFlutterPointerDeviceKindTouch,
        kFlutterPointerSignalKindNone, lx, ly,
        event->touch_id + 1,
        seat_get_flutter_timestamp(event->time_msec));
}

void on_server_cursor_touch_up(struct wl_listener *listener,
    void *data)
{
    const Core *instance = Core::instance();
    const struct wlr_touch_up_event *event =
        static_cast<wlr_touch_up_event*>(data);

    if (instance->engine == nullptr)
    {
        return;
    }

    auto point = seat_touch_point_get(event->touch_id);
    if (!point)
    {
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

    send_flutter_input_event(
        kUp, kFlutterPointerDeviceKindTouch, kFlutterPointerSignalKindNone,
        point->sx, point->sy, event->touch_id + 1,
        seat_get_flutter_timestamp(event->time_msec));
}

void on_server_cursor_touch_motion(struct wl_listener *listener,
    void *data)
{
    const Core *instance = Core::instance();
    struct wlr_touch_motion_event *event =
        static_cast<wlr_touch_motion_event*>(data);

    if (instance->engine == nullptr)
    {
        return;
    }

    double lx = 0.0, ly = 0.0;
    wlr_cursor_absolute_to_layout_coords(instance->cursor, &event->touch->base,
        event->x, event->y, &lx, &ly);

    auto point = seat_touch_point_get(event->touch_id);
    if (!point)
    {
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

    send_flutter_input_event(kMove, kFlutterPointerDeviceKindTouch,
        kFlutterPointerSignalKindNone, lx, ly,
        event->touch_id + 1,
        seat_get_flutter_timestamp(event->time_msec));
}

void on_server_cursor_touch_cancel(struct wl_listener *listener,
    void *data)
{
    const Core *instance = Core::instance();
    const struct wlr_touch_cancel_event *event =
        static_cast<wlr_touch_cancel_event*>(data);

    if (instance->engine == nullptr)
    {
        return;
    }

    auto point = seat_touch_point_get(event->touch_id);
    if (!point)
    {
        return;
    }

    if ((point->event_mask != TouchEventkDown) &&
        (point->event_mask != TouchEventkMotion) &&
        (point->event_mask != TouchEventkUp))
    {
        return;
    }

    point->event_mask = TouchEventkCancel;

    send_flutter_input_event(
        kCancel, kFlutterPointerDeviceKindTouch, kFlutterPointerSignalKindNone,
        point->sx, point->sy, event->touch_id + 1,
        seat_get_flutter_timestamp(event->time_msec));
}

void on_server_cursor_touch_frame(struct wl_listener *listener,
    void *data)
{
    (void)listener;
    (void)data;
    Core *instance = Core::instance();
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
    Core *instance = Core::instance();

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
    Core *instance = Core::instance();
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
    Core *instance = Core::instance();

    wlr_cursor_attach_input_device(instance->cursor, device);

    map_touch_to_output(device);

    // wlr_touch_init(wlr_touch, &touch_impl, "stipc_touch");
    seat_configure_libinput_device(device);

    if (!wlr_input_device_is_libinput(device))
    {
        wlr_log(WLR_DEBUG, "Touch device is not a libinput device. (%s) (%i)", device->name,
            device->type);
        return;
    }

    auto libinput_dev = wlr_libinput_get_device_handle(device);
    float m[6];

    libinput_device_config_calibration_get_default_matrix(libinput_dev, m);
    libinput_device_config_calibration_set_matrix(libinput_dev, m);
}
