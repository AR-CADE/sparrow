#include "seat_message.hpp"
#include "flutter_embedder.h"
#include "flutter/platform/key_mapping.hpp"
#include "input/seat.hpp"
#include <cinttypes>
#include <core.hpp>
#include <wlr/types/wlr_keyboard.h>

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

    if (!instance->pigeon_flutter_api)
    {
        return;
    }

    instance->pigeon_flutter_api->GestureSwipeBegin(
        fingers, time_msec,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "gesture_swipe_begin error: %s", err.message().c_str());
    });
}

void send_gesture_swipe_update(double dx, double dy, uint32_t time_msec)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api)
    {
        return;
    }

    instance->pigeon_flutter_api->GestureSwipeUpdate(
        dx, dy, time_msec,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "gesture_swipe_update error: %s", err.message().c_str());
    });
}

void send_gesture_swipe_end(bool cancelled, uint32_t time_msec)
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return;
    }

    instance->gesture_active = false;

    if (!instance->pigeon_flutter_api)
    {
        return;
    }

    instance->pigeon_flutter_api->GestureSwipeEnd(
        cancelled, time_msec,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "gesture_swipe_end error: %s", err.message().c_str());
    });
}

void send_zoom_scroll(double delta_y, double x, double y)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api)
    {
        return;
    }

    instance->pigeon_flutter_api->ZoomScroll(
        delta_y, x, y,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "zoom_scroll error: %s", err.message().c_str());
    });
}

void send_zoom_key(int action)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api)
    {
        return;
    }

    instance->pigeon_flutter_api->ZoomKey(
        action,
        instance->cursor ? instance->cursor->x : 0.0,
        instance->cursor ? instance->cursor->y : 0.0,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "zoom_key error: %s", err.message().c_str());
    });
}

bool send_flutter_key_event(uint32_t xkb_keycode, uint32_t sym,
    uint32_t unicode, bool pressed, uint32_t modifiers, uint32_t time_msec)
{
    Core *instance = Core::instance();
    if ((instance == nullptr) || (instance->engine == nullptr) ||
        (instance->embedder_api.SendKeyEvent == nullptr))
    {
        return false;
    }

    uint64_t physical = sparrow_xkb_to_physical_key(xkb_keycode);
    uint64_t logical  = sparrow_keysym_to_logical_key(sym);

    char char_buf[5] = {0};
    const char *character_ptr = nullptr;

    if (pressed && (unicode > 0) && (unicode != 0x7F) &&
        ((unicode >= 0x20) || (unicode == '\t') || (unicode == '\r') || (unicode == '\n')))
    {
        if (unicode < 0x80)
        {
            char_buf[0] = static_cast<char>(unicode);
            char_buf[1] = '\0';
        } else if (unicode < 0x800)
        {
            char_buf[0] = static_cast<char>(0xC0 | (unicode >> 6));
            char_buf[1] = static_cast<char>(0x80 | (unicode & 0x3F));
            char_buf[2] = '\0';
        } else if (unicode < 0x10000)
        {
            char_buf[0] = static_cast<char>(0xE0 | (unicode >> 12));
            char_buf[1] = static_cast<char>(0x80 | ((unicode >> 6) & 0x3F));
            char_buf[2] = static_cast<char>(0x80 | (unicode & 0x3F));
            char_buf[3] = '\0';
        } else if (unicode < 0x110000)
        {
            char_buf[0] = static_cast<char>(0xF0 | (unicode >> 18));
            char_buf[1] = static_cast<char>(0x80 | ((unicode >> 12) & 0x3F));
            char_buf[2] = static_cast<char>(0x80 | ((unicode >> 6) & 0x3F));
            char_buf[3] = static_cast<char>(0x80 | (unicode & 0x3F));
            char_buf[4] = '\0';
        }

        character_ptr = char_buf;
    }

    FlutterKeyEvent fl_key = {};
    fl_key.struct_size = sizeof(FlutterKeyEvent);
    fl_key.timestamp   = static_cast<double>(seat_get_flutter_timestamp(time_msec));
    fl_key.type     = pressed ? kFlutterKeyEventTypeDown : kFlutterKeyEventTypeUp;
    fl_key.physical = physical;
    fl_key.logical  = logical;
    fl_key.character   = character_ptr;
    fl_key.synthesized = false;
    fl_key.device_type = kFlutterKeyEventDeviceTypeKeyboard;

    FlutterEngineResult res = instance->embedder_api.SendKeyEvent(
        instance->engine, &fl_key, nullptr, nullptr);

    uint32_t xkb_mods = (modifiers & 0x1F);
    if ((modifiers & WLR_MODIFIER_LOGO) != 0)
    {
        xkb_mods |= (1 << 26);
    }

    if (instance->embedder_api.SendPlatformMessage != nullptr)
    {
        char json_buf[512];
        if (unicode != 0)
        {
            snprintf(json_buf, sizeof(json_buf),
                "{\"type\":\"%s\",\"keymap\":\"linux\",\"scanCode\":%u,\"toolkit\":\"gtk\",\"keyCode\":%u,\"modifiers\":%u,\"unicodeScalarValues\":%u,\"specifiedLogicalKey\":%"
                PRIu64 "}",
                pressed ? "keydown" : "keyup",
                xkb_keycode,
                sym,
                xkb_mods,
                unicode,
                logical);
        } else
        {
            snprintf(json_buf, sizeof(json_buf),
                "{\"type\":\"%s\",\"keymap\":\"linux\",\"scanCode\":%u,\"toolkit\":\"gtk\",\"keyCode\":%u,\"modifiers\":%u,\"specifiedLogicalKey\":%"
                PRIu64 "}",
                pressed ? "keydown" : "keyup",
                xkb_keycode,
                sym,
                xkb_mods,
                logical);
        }

        FlutterPlatformMessage platform_msg = {};
        platform_msg.struct_size = sizeof(FlutterPlatformMessage);
        platform_msg.channel     = "flutter/keyevent";
        platform_msg.message     = reinterpret_cast<const uint8_t*>(json_buf);
        platform_msg.message_size    = strlen(json_buf);
        platform_msg.response_handle = nullptr;

        FlutterEngineResult res_plat =
            instance->embedder_api.SendPlatformMessage(instance->engine, &platform_msg);

        wlr_log(WLR_INFO,
            "[KEY] send_flutter_key_event: xkb_keycode=%u, sym=0x%x, physical=0x%" PRIx64 ", logical=0x%"
            PRIx64 ", pressed=%d, mods=0x%x -> res_key=%d, res_plat=%d, json=%s",
            xkb_keycode, sym, physical, logical, (int)pressed, xkb_mods, (int)res, (int)res_plat, json_buf);
    } else
    {
        wlr_log(WLR_INFO,
            "[KEY] send_flutter_key_event: xkb_keycode=%u, sym=0x%x, physical=0x%" PRIx64 ", logical=0x%"
            PRIx64 ", pressed=%d, mods=0x%x -> res_key=%d (no SendPlatformMessage)",
            xkb_keycode, sym, physical, logical, (int)pressed, xkb_mods, (int)res);
    }

    return res == kSuccess;
}
