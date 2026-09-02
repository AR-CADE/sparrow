#include "seat_message.hpp"
#include "flutter_embedder.h"
#include <core.hpp>

void send_flutter_input_event(
    FlutterPointerPhase phase, FlutterPointerDeviceKind device_kind,
    FlutterPointerSignalKind signal_kind, double x, double y,
    int device, size_t timestamp, int buttons,
    double scroll_delta_x, double scroll_delta_y, int view_id,
    double pan_x, double pan_y, double rotation)
{
    Core *instance = Core::instance();
    if ((instance == nullptr) || (instance->engine == nullptr))
    {
        return;
    }

    FlutterPointerEvent pointer_event = {};
    pointer_event.struct_size = sizeof(FlutterPointerEvent);
    pointer_event.phase     = phase;
    pointer_event.timestamp = timestamp;
    pointer_event.x = x;
    pointer_event.y = y;
    pointer_event.device  = device;
    pointer_event.view_id = view_id;
    pointer_event.signal_kind    = signal_kind;
    pointer_event.scroll_delta_x = scroll_delta_x;
    pointer_event.scroll_delta_y = scroll_delta_y;
    pointer_event.device_kind    = device_kind;
    pointer_event.buttons  = buttons;
    pointer_event.pan_x    = pan_x;
    pointer_event.pan_y    = pan_y;
    pointer_event.rotation = rotation;

    instance->embedder_api.SendPointerEvent(instance->engine, &pointer_event, 1);
}

void send_flutter_mouse_event(FlutterPointerPhase phase,
    FlutterPointerSignalKind signal_kind,
    double scroll_delta_x,
    double scroll_delta_y, size_t timestamp,
    int view_id, double pan_x,
    double pan_y, double rotation)
{
    const Core *instance = Core::instance();
    if ((instance == nullptr) || (instance->engine == nullptr))
    {
        return;
    }

    send_flutter_input_event(
        phase, kFlutterPointerDeviceKindMouse, signal_kind, instance->cursor->x,
        instance->cursor->y, 0, timestamp, instance->input.fl_mouse_button_mask,
        scroll_delta_x, scroll_delta_y, view_id, pan_x, pan_y, rotation);
}

void send_flutter_trackpad_event(FlutterPointerPhase phase,
    size_t timestamp, double pan_x,
    double pan_y, double scale,
    double rotation, int view_id)
{
    Core *instance = Core::instance();
    if ((instance == nullptr) || (instance->engine == nullptr))
    {
        return;
    }

    FlutterPointerEvent fl_event = {};
    fl_event.struct_size = sizeof(fl_event);
    fl_event.timestamp   = timestamp;
    fl_event.x     = instance->cursor->x;
    fl_event.y     = instance->cursor->y;
    fl_event.phase = phase;
    fl_event.pan_x = pan_x;
    fl_event.pan_y = pan_y;
    fl_event.scale = scale;
    fl_event.rotation = rotation;
    fl_event.device   = 1;
    fl_event.device_kind = kFlutterPointerDeviceKindTrackpad;
    fl_event.signal_kind = kFlutterPointerSignalKindNone;
    fl_event.view_id     = view_id;

    instance->embedder_api.SendPointerEvent(instance->engine, &fl_event, 1);
}

void send_gesture_swipe_begin(uint32_t fingers, uint32_t time_msec)
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return;
    }

    instance->gesture_active = true;

    if (!instance->wlroots_channel)
    {
        return;
    }

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("fingers"), flutter::EncodableValue((int64_t)fingers)},
        {flutter::EncodableValue("time_msec"), flutter::EncodableValue((int64_t)time_msec)},
    };

    instance->wlroots_channel->InvokeMethod("gesture_swipe_begin",
        std::make_unique<flutter::EncodableValue>(map));
}

void send_gesture_swipe_update(double dx, double dy, uint32_t time_msec)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel)
    {
        return;
    }

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("dx"), flutter::EncodableValue(dx)},
        {flutter::EncodableValue("dy"), flutter::EncodableValue(dy)},
        {flutter::EncodableValue("time_msec"), flutter::EncodableValue((int64_t)time_msec)},
    };

    instance->wlroots_channel->InvokeMethod("gesture_swipe_update",
        std::make_unique<flutter::EncodableValue>(map));
}

void send_gesture_swipe_end(bool cancelled, uint32_t time_msec)
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return;
    }

    instance->gesture_active = false;

    if (!instance->wlroots_channel)
    {
        return;
    }

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("cancelled"), flutter::EncodableValue((int64_t)(cancelled ? 1 : 0))},
        {flutter::EncodableValue("time_msec"), flutter::EncodableValue((int64_t)time_msec)},
    };

    instance->wlroots_channel->InvokeMethod("gesture_swipe_end",
        std::make_unique<flutter::EncodableValue>(map));
}
