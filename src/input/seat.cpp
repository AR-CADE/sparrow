#include "flutter_embedder.h"
#include <cstdint>
#include <linux/input-event-codes.h>
#include <memory>
#include <optional>

#include <sparrow/nonstd/wlroots-full.hpp>

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "core.hpp"
#include "flutter/platform/cursor.hpp"
#include "flutter/platform/engine/messages/seat_message.hpp"
#include "flutter/platform/text_input.hpp"
#include "keyboard.hpp"
#include "libinput.h"
#include "pointer.hpp"
#include "renderer/shaders.hpp"
#include "seat.hpp"
#include "surface/popup.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"

static std::vector<std::shared_ptr<touch_point>> _touch_event;

SparrowView *seat_get_focus()
{
    Core *instance = Core::instance();

    const struct wlr_surface *prev_surface =
        instance->seat->keyboard_state.focused_surface;
    if (!prev_surface)
    {
        return nullptr;
    }

    return instance->find_view_by_wlr_surface(prev_surface);
}

std::shared_ptr<touch_point> seat_touch_point_add(int32_t id)
{
    for (auto it = _touch_event.begin(); it != _touch_event.end();)
    {
        auto point = *it;
        if (point->id == id)
        {
            return point;
        } else
        {
            ++it;
        }
    }

    auto point = std::make_shared<touch_point>(id);
    _touch_event.push_back(point);

    return point;
}

void seat_touch_point_delete(int32_t id)
{
    for (auto it = _touch_event.begin(); it != _touch_event.end();)
    {
        auto point = *it;

        if (point->id == id)
        {
            it = _touch_event.erase(it);
        } else
        {
            ++it;
        }
    }
}

std::shared_ptr<touch_point> seat_touch_point_get(int32_t id)
{
    for (auto it = _touch_event.begin(); it != _touch_event.end();)
    {
        auto point = *it;

        if (point->id == id)
        {
            return point;
        } else
        {
            ++it;
        }
    }

    return nullptr;
}

[[maybe_unused]] static struct wlr_scene_node *seat_desktop_view_at(double lx, double ly,
    struct wlr_surface **surface,
    double *sx, double *sy)
{
    Core *instance = Core::instance();
    // wlr_log(WLR_INFO, "desktop_view_at");

    struct wlr_scene_node *node =
        wlr_scene_node_at(&instance->scene->tree.node, lx, ly, sx, sy);
    if ((node == nullptr) || (node->type != WLR_SCENE_NODE_BUFFER))
    {
        return nullptr;
    }

    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    const struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface)
    {
        return nullptr;
    }

    *surface = scene_surface->surface;

    /* Walk up the tree until we find a node with a data pointer. When done, we've found the node representing
     * the view. */
    while (!node->data)
    {
        if (!node->parent)
        {
            node = nullptr;
            break;
        }

        node = &node->parent->node;
    }

    // assert(node != nullptr);
    return node;
}

int64_t seat_flutter_button_mask_from_linux(uint32_t button)
{
    switch (button)
    {
      case BTN_LEFT:
        return kFlutterPointerButtonMousePrimary;

      case BTN_RIGHT:
        return kFlutterPointerButtonMouseSecondary;

      case BTN_MIDDLE:
        return kFlutterPointerButtonMouseMiddle;

      case BTN_SIDE:
      case BTN_BACK:
        return kFlutterPointerButtonMouseBack;

      case BTN_EXTRA:
      case BTN_FORWARD:
        return kFlutterPointerButtonMouseForward;

      case BTN_TASK:
        return (1LL << 5);

      default:
        if ((button >= BTN_MOUSE) && (button <= BTN_TASK))
        {
            return (1LL << (button - BTN_MOUSE));
        }

        if ((button >= BTN_MISC) && (button < BTN_MOUSE))
        {
            return (1LL << (8 + (button - BTN_MISC)));
        }

        return 0;
    }
}

uint64_t seat_get_flutter_timestamp(uint32_t wl_time_msec)
{
    (void)wl_time_msec;
    Core *instance = Core::instance();
    if (instance && instance->engine && instance->embedder_api.GetCurrentTime)
    {
        return instance->embedder_api.GetCurrentTime() / 1000ULL;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void on_seat_request_set_selection(struct wl_listener *listener,
    void *data)
{
    (void)listener;
    Core *instance = Core::instance();
    struct wlr_seat_request_set_selection_event *event =
        static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(instance->seat, event->source, event->serial);
}

static void on_seat_request_set_primary_selection(struct wl_listener *listener,
    void *data)
{
    (void)listener;
    Core *instance = Core::instance();
    struct wlr_seat_request_set_primary_selection_event *event =
        static_cast<wlr_seat_request_set_primary_selection_event*>(data);
    wlr_seat_set_primary_selection(instance->seat, event->source, event->serial);
}

void sparrow_seat_update_capabilities()
{
    Core *instance = Core::instance();
    if (!instance || !instance->seat)
    {
        return;
    }

    uint32_t caps = 0;

    if (!wl_list_empty(&instance->keyboards))
    {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }

    if (!wl_list_empty(&instance->pointers))
    {
        caps |= WL_SEAT_CAPABILITY_POINTER;
    }

    if (!wl_list_empty(&instance->touchs))
    {
        caps |= WL_SEAT_CAPABILITY_TOUCH;
    }

    wlr_seat_set_capabilities(instance->seat, caps);

    /* Hide cursor if the seat doesn't have pointer capability. */
    if ((caps & WL_SEAT_CAPABILITY_POINTER) == 0)
    {
        wlr_cursor_unset_image(instance->cursor);
    } else
    {
        wlr_cursor_set_xcursor(instance->cursor, instance->cursor_mgr, "left_ptr");
    }
}

#define TOUCHPAD_CLICK_METHOD_NONE -1
#define TOUCHPAD_CLICK_METHOD_DEFAULT 0
#define TOUCHPAD_CLICK_METHOD_BUTTON_AREAS 1
#define TOUCHPAD_CLICK_METHOD_CLICKFINGER 2

#define TOUCHPAD_SCROLL_METHOD_NONE -1
#define TOUCHPAD_SCROLL_METHOD_DEFAULT 0
#define TOUCHPAD_SCROLL_METHOD_BUTTON_AREAS 1
#define TOUCHPAD_SCROLL_METHOD_TWO_FINGER 2
#define TOUCHPAD_SCROLL_METHOD_EDGE 3
#define TOUCHPAD_SCROLL_METHOD_ON_BUTTON_DOWN 4
#define TOUCHPAD_SCROLL_METHOD_CLICKFINGER 2

#define TOUCHPAD_MULTI_FINGER_DRAG_NONE -1
#define TOUCHPAD_MULTI_FINGER_DRAG_DEFAULT 0
#define TOUCHPAD_MULTI_FINGER_DRAG_3FG 1
#define TOUCHPAD_MULTI_FINGER_DRAG_4FG 2

#define LIBINPUT_ACCEL_PROFILE_NONE -1
#define LIBINPUT_ACCEL_PROFILE_DEFAULT 0
#define LIBINPUT_ACCEL_PROFILE_ADAPTIVE 1
#define LIBINPUT_ACCEL_PROFILE_FLAT 2

struct input_profile
{
    bool touchpad_tap_enabled;
    bool left_handed_mode;
    double touchpad_cursor_speed;
    bool disable_touchpad_while_typing;
    int touchpad_click_method;
    int touchpad_scroll_method;
    bool touchpad_dwmouse_enabled;
    bool touchpad_tap_and_drag_enabled;
    bool touchpad_drag_lock_enabled;
    int touchpad_multi_finger_drag;
    bool touchpad_natural_scroll_enabled;
    double mouse_cursor_speed;
    bool mouse_natural_scroll_enabled;
    int touchpad_accel_profile;
    int mouse_accel_profile;
};

static void set_libinput_accel_profile(struct libinput_device *dev,
    int profile)
{
    if (profile == LIBINPUT_ACCEL_PROFILE_DEFAULT)
    {
        libinput_device_config_accel_set_profile(
            dev, libinput_device_config_accel_get_default_profile(dev));
    } else if (profile == LIBINPUT_ACCEL_PROFILE_NONE)
    {
        libinput_device_config_accel_set_profile(
            dev, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);
    } else if (profile == LIBINPUT_ACCEL_PROFILE_ADAPTIVE)
    {
        libinput_device_config_accel_set_profile(
            dev, LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
    } else if (profile == LIBINPUT_ACCEL_PROFILE_FLAT)
    {
        libinput_device_config_accel_set_profile(
            dev, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
    } else
    {
        wlr_log(WLR_ERROR, "Invalid libinput acceleration profile.");
    }
}

void seat_configure_libinput_device(struct wlr_input_device *device)
{
    if (!wlr_input_device_is_libinput(device))
    {
        wlr_log(WLR_DEBUG, "Input device is not a libinput device. (%s) (%i)",
            device->name, device->type);
        return;
    }

    struct input_profile profile;

    profile.touchpad_tap_enabled = true;
    profile.left_handed_mode     = false;
    profile.touchpad_cursor_speed = 0.0;
    profile.disable_touchpad_while_typing = false;
    profile.touchpad_click_method    = TOUCHPAD_CLICK_METHOD_DEFAULT;
    profile.touchpad_scroll_method   = TOUCHPAD_SCROLL_METHOD_DEFAULT;
    profile.touchpad_dwmouse_enabled = false;
    profile.touchpad_tap_and_drag_enabled = true;
    profile.touchpad_drag_lock_enabled    = false;
    profile.touchpad_multi_finger_drag    = TOUCHPAD_MULTI_FINGER_DRAG_DEFAULT;
    profile.touchpad_natural_scroll_enabled = false;
    profile.mouse_natural_scroll_enabled    = false;
    profile.mouse_cursor_speed     = 0.0;
    profile.touchpad_accel_profile = LIBINPUT_ACCEL_PROFILE_DEFAULT;
    profile.mouse_accel_profile    = LIBINPUT_ACCEL_PROFILE_DEFAULT;

    struct libinput_device *dev = wlr_libinput_get_device_handle(device);
    if (dev == nullptr)
    {
        return;
    }

    libinput_device_config_left_handed_set(dev, profile.left_handed_mode);

    libinput_device_config_tap_set_enabled(
        dev, profile.touchpad_tap_enabled ? LIBINPUT_CONFIG_TAP_ENABLED :
        LIBINPUT_CONFIG_TAP_DISABLED);

    if (libinput_device_config_tap_get_finger_count(dev) > 0)
    {
        libinput_device_config_accel_set_speed(dev, profile.touchpad_cursor_speed);

        set_libinput_accel_profile(dev, profile.touchpad_accel_profile);
        libinput_device_config_tap_set_enabled(
            dev, profile.touchpad_tap_enabled ? LIBINPUT_CONFIG_TAP_ENABLED :
            LIBINPUT_CONFIG_TAP_DISABLED);
        libinput_device_config_tap_set_button_map(dev, LIBINPUT_CONFIG_TAP_MAP_LRM);

        if (profile.touchpad_click_method == TOUCHPAD_CLICK_METHOD_DEFAULT)
        {
            libinput_device_config_click_set_method(
                dev, libinput_device_config_click_get_default_method(dev));
        } else if (profile.touchpad_click_method == TOUCHPAD_CLICK_METHOD_NONE)
        {
            libinput_device_config_click_set_method(
                dev, LIBINPUT_CONFIG_CLICK_METHOD_NONE);
        } else if (profile.touchpad_click_method ==
                   TOUCHPAD_CLICK_METHOD_BUTTON_AREAS)
        {
            libinput_device_config_click_set_method(
                dev, LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
        } else if (profile.touchpad_click_method ==
                   TOUCHPAD_CLICK_METHOD_CLICKFINGER)
        {
            libinput_device_config_click_set_method(
                dev, LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
        } else
        {
            wlr_log(WLR_ERROR, "Invalid libinput click method.");
        }

        if (profile.touchpad_scroll_method == TOUCHPAD_SCROLL_METHOD_DEFAULT)
        {
            libinput_device_config_scroll_set_method(
                dev, libinput_device_config_scroll_get_default_method(dev));
        } else if (profile.touchpad_scroll_method == TOUCHPAD_SCROLL_METHOD_NONE)
        {
            libinput_device_config_scroll_set_method(
                dev, LIBINPUT_CONFIG_SCROLL_NO_SCROLL);
        } else if (profile.touchpad_scroll_method ==
                   TOUCHPAD_SCROLL_METHOD_TWO_FINGER)
        {
            libinput_device_config_scroll_set_method(dev, LIBINPUT_CONFIG_SCROLL_2FG);
        } else if (profile.touchpad_scroll_method == TOUCHPAD_SCROLL_METHOD_EDGE)
        {
            libinput_device_config_scroll_set_method(dev,
                LIBINPUT_CONFIG_SCROLL_EDGE);
        } else if (profile.touchpad_scroll_method ==
                   TOUCHPAD_SCROLL_METHOD_ON_BUTTON_DOWN)
        {
            libinput_device_config_scroll_set_method(
                dev, LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
        } else
        {
            wlr_log(WLR_ERROR, "Invalid libinput scroll method.");
        }

        libinput_device_config_dwt_set_enabled(dev,
            profile.disable_touchpad_while_typing ?
            LIBINPUT_CONFIG_DWT_ENABLED :
            LIBINPUT_CONFIG_DWT_DISABLED);

        libinput_device_config_send_events_set_mode(
            dev, profile.touchpad_dwmouse_enabled ?
            LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE :
            LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);

        libinput_device_config_tap_set_drag_enabled(
            dev, profile.touchpad_tap_and_drag_enabled ?
            LIBINPUT_CONFIG_DRAG_ENABLED :
            LIBINPUT_CONFIG_DRAG_DISABLED);

        libinput_device_config_tap_set_drag_lock_enabled(
            dev, profile.touchpad_drag_lock_enabled ?
            LIBINPUT_CONFIG_DRAG_LOCK_ENABLED :
            LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);

        if (profile.touchpad_multi_finger_drag ==
            TOUCHPAD_MULTI_FINGER_DRAG_DEFAULT)
        {
#if HAVE_LIBINPUT_3FG_DRAG
            libinput_device_config_3fg_drag_set_enabled(
                dev, libinput_device_config_3fg_drag_get_default_enabled(dev));
#endif
        } else if (profile.touchpad_multi_finger_drag ==
                   TOUCHPAD_MULTI_FINGER_DRAG_NONE)
        {
#if HAVE_LIBINPUT_3FG_DRAG
            libinput_device_config_3fg_drag_set_enabled(
                dev, LIBINPUT_CONFIG_3FG_DRAG_DISABLED);
#else
            wlr_log(
                WLR_INFO,
                "Multi-finger drag not implemented with current libinput version.");
#endif
        } else if (profile.touchpad_multi_finger_drag ==
                   TOUCHPAD_MULTI_FINGER_DRAG_3FG)
        {
#if HAVE_LIBINPUT_3FG_DRAG
            libinput_device_config_3fg_drag_set_enabled(
                dev, LIBINPUT_CONFIG_3FG_DRAG_ENABLED_3FG);
#else
            wlr_log(
                WLR_INFO,
                "Multi-finger drag not implemented with current libinput version.");
#endif
        } else if (profile.touchpad_multi_finger_drag ==
                   TOUCHPAD_MULTI_FINGER_DRAG_4FG)
        {
#if HAVE_LIBINPUT_3FG_DRAG
            libinput_device_config_3fg_drag_set_enabled(
                dev, LIBINPUT_CONFIG_3FG_DRAG_ENABLED_4FG);
#else
            wlr_log(
                WLR_INFO,
                "Multi-finger drag not implemented with current libinput version.");
#endif
        } else
        {
            wlr_log(WLR_ERROR, "Invalid libinput multi-finger drag value.");
        }

        if (libinput_device_config_scroll_has_natural_scroll(dev) > 0)
        {
            libinput_device_config_scroll_set_natural_scroll_enabled(
                dev, profile.touchpad_natural_scroll_enabled);
        }
    } else
    {
        libinput_device_config_accel_set_speed(dev, profile.mouse_cursor_speed);
        set_libinput_accel_profile(dev, profile.mouse_accel_profile);

        if (libinput_device_config_scroll_has_natural_scroll(dev) > 0)
        {
            libinput_device_config_scroll_set_natural_scroll_enabled(
                dev, profile.mouse_natural_scroll_enabled);
        }
    }
}

static void on_server_new_input(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct wlr_input_device *device = static_cast<wlr_input_device*>(data);

    switch (device->type)
    {
      case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(device);
        break;

      case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(device);
        break;

      case WLR_INPUT_DEVICE_TOUCH:
        server_new_touch(device);
        break;

      case WLR_INPUT_DEVICE_TABLET:
        break;

      default:
        break;
    }

    sparrow_seat_update_capabilities();
}

void sparrow_seat_init()
{
    Core *instance = Core::instance();

    instance->input.mouse_button_mask    = 0;
    instance->input.fl_mouse_button_mask = 0;

    instance->seat = wlr_seat_create(instance->wl_display, "seat0");

    instance->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(instance->cursor, instance->output_layout);

    wlr_cursor_map_to_output(instance->cursor, nullptr);
    wlr_cursor_warp(instance->cursor, nullptr, instance->cursor->x,
        instance->cursor->y);

    const char *cursor_theme    = getenv("XCURSOR_THEME");
    unsigned int cursor_size    = 24;
    const char *env_cursor_size = getenv("XCURSOR_SIZE");
    if (env_cursor_size != nullptr)
    {
        int sz = atoi(env_cursor_size);
        if (sz > 0)
        {
            cursor_size = static_cast<unsigned int>(sz);
        }
    }

    instance->cursor_mgr = wlr_xcursor_manager_create(cursor_theme, cursor_size);
    wlr_xcursor_manager_load(instance->cursor_mgr, 1);

    // Initialize cursor-shape-v1 manager (used by GTK4 and modern Wayland
    // clients)
    instance->cursor_shape_mgr =
        wlr_cursor_shape_manager_v1_create(instance->wl_display, 2);
    if (instance->cursor_shape_mgr != nullptr)
    {
        instance->request_set_cursor_shape.notify =
            on_seat_request_set_cursor_shape;
        wl_signal_add(&instance->cursor_shape_mgr->events.request_set_shape,
            &instance->request_set_cursor_shape);
    }

    // Initialize default cursor
    instance->current_xcursor_name  = std::string("left_ptr");
    instance->client_cursor_surface = nullptr;
    wl_list_init(&instance->client_cursor_destroy.link);
    instance->client_cursor_hotspot_x = 0;
    instance->client_cursor_hotspot_y = 0;

    wlr_cursor_set_xcursor(instance->cursor, instance->cursor_mgr, "left_ptr");

    instance->request_cursor.notify = on_seat_request_cursor;
    wl_signal_add(&instance->seat->events.request_set_cursor,
        &instance->request_cursor);

    // Clipboard selection handlers - allows clients to copy/paste
    instance->request_set_selection.notify = on_seat_request_set_selection;
    wl_signal_add(&instance->seat->events.request_set_selection,
        &instance->request_set_selection);

    // Primary selection (middle-click paste)
    instance->request_set_primary_selection.notify =
        on_seat_request_set_primary_selection;
    wl_signal_add(&instance->seat->events.request_set_primary_selection,
        &instance->request_set_primary_selection);

    instance->cursor_motion.notify = on_server_cursor_motion;
    wl_signal_add(&instance->cursor->events.motion, &instance->cursor_motion);
    instance->cursor_motion_absolute.notify = on_server_cursor_motion_absolute;
    wl_signal_add(&instance->cursor->events.motion_absolute,
        &instance->cursor_motion_absolute);
    instance->cursor_button.notify = on_server_cursor_button;
    wl_signal_add(&instance->cursor->events.button, &instance->cursor_button);
    instance->cursor_axis.notify = on_server_cursor_axis;
    wl_signal_add(&instance->cursor->events.axis, &instance->cursor_axis);
    instance->cursor_frame.notify = on_server_cursor_frame;
    wl_signal_add(&instance->cursor->events.frame, &instance->cursor_frame);

    wl_signal_add(&instance->cursor->events.swipe_begin, &instance->swipe_begin);
    instance->swipe_begin.notify = handle_swipe_begin;

    wl_signal_add(&instance->cursor->events.swipe_update,
        &instance->swipe_update);
    instance->swipe_update.notify = handle_swipe_update;

    wl_signal_add(&instance->cursor->events.swipe_end, &instance->swipe_end);
    instance->swipe_end.notify = handle_swipe_end;

    instance->pinch_begin.notify = handle_pinch_begin;
    wl_signal_add(&instance->cursor->events.pinch_begin, &instance->pinch_begin);

    instance->pinch_update.notify = handle_pinch_update;
    wl_signal_add(&instance->cursor->events.pinch_update,
        &instance->pinch_update);

    instance->pinch_end.notify = handle_pinch_end;
    wl_signal_add(&instance->cursor->events.pinch_end, &instance->pinch_end);

    instance->hold_begin.notify = handle_hold_begin;
    wl_signal_add(&instance->cursor->events.hold_begin, &instance->hold_begin);

    instance->hold_end.notify = handle_hold_end;
    wl_signal_add(&instance->cursor->events.hold_end, &instance->hold_end);

    instance->cursor_touch_down.notify = on_server_cursor_touch_down;
    wl_signal_add(&instance->cursor->events.touch_down,
        &instance->cursor_touch_down);
    instance->cursor_touch_up.notify = on_server_cursor_touch_up;
    wl_signal_add(&instance->cursor->events.touch_up, &instance->cursor_touch_up);
    instance->cursor_touch_motion.notify = on_server_cursor_touch_motion;
    wl_signal_add(&instance->cursor->events.touch_motion,
        &instance->cursor_touch_motion);
    instance->cursor_touch_frame.notify = on_server_cursor_touch_frame;
    wl_signal_add(&instance->cursor->events.touch_frame,
        &instance->cursor_touch_frame);
    instance->cursor_touch_cancel.notify = on_server_cursor_touch_cancel;
    wl_signal_add(&instance->cursor->events.touch_cancel,
        &instance->cursor_touch_cancel);

    wl_list_init(&instance->keyboards);
    wl_list_init(&instance->touchs);
    wl_list_init(&instance->pointers);

    instance->new_input.notify = on_server_new_input;
    wl_signal_add(&instance->backend->events.new_input, &instance->new_input);
}

void sparrow_seat_finish()
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return;
    }

    struct sparrow_pointer *pointer, *tmp_pointer;
    wl_list_for_each_safe(pointer, tmp_pointer, &instance->pointers, link)
    {
        wl_list_remove(&pointer->link);
        wlr_cursor_detach_input_device(instance->cursor, &pointer->pointer->base);
        wl_list_remove(&pointer->destroy.link);
        delete pointer;
    }

    struct sparrow_keyboard *keyboard, *tmp_keyboard;
    wl_list_for_each_safe(keyboard, tmp_keyboard, &instance->keyboards, link)
    {
        if (keyboard->compose_state != nullptr)
        {
            xkb_compose_state_unref(keyboard->compose_state);
            keyboard->compose_state = nullptr;
        }

        wl_list_remove(&keyboard->modifiers.link);
        wl_list_remove(&keyboard->key.link);
        wl_list_remove(&keyboard->destroy.link);
        wl_list_remove(&keyboard->link);
        delete keyboard;
    }

    struct sparrow_touch *touch, *tmp_touch;
    wl_list_for_each_safe(touch, tmp_touch, &instance->touchs, link)
    {
        wl_list_remove(&touch->destroy.link);
        wl_list_remove(&touch->link);
        delete touch;
    }

    if (instance->seat)
    {
        wlr_seat_set_capabilities(instance->seat, 0);
        instance->seat = nullptr;
    }

    if (instance->request_cursor.link.prev)
    {
        wl_list_remove(&instance->request_cursor.link);
    }

    if (instance->request_set_cursor_shape.link.prev)
    {
        wl_list_remove(&instance->request_set_cursor_shape.link);
    }

    if (instance->client_cursor_destroy.link.prev &&
        instance->client_cursor_destroy.link.next)
    {
        wl_list_remove(&instance->client_cursor_destroy.link);
        wl_list_init(&instance->client_cursor_destroy.link);
    }

    instance->client_cursor_surface = nullptr;

    if (instance->request_set_selection.link.prev)
    {
        wl_list_remove(&instance->request_set_selection.link);
    }

    if (instance->request_set_primary_selection.link.prev)
    {
        wl_list_remove(&instance->request_set_primary_selection.link);
    }

    if (instance->cursor_motion.link.prev)
    {
        wl_list_remove(&instance->cursor_motion.link);
    }

    if (instance->cursor_motion_absolute.link.prev)
    {
        wl_list_remove(&instance->cursor_motion_absolute.link);
    }

    if (instance->cursor_button.link.prev)
    {
        wl_list_remove(&instance->cursor_button.link);
    }

    if (instance->cursor_axis.link.prev)
    {
        wl_list_remove(&instance->cursor_axis.link);
    }

    if (instance->cursor_frame.link.prev)
    {
        wl_list_remove(&instance->cursor_frame.link);
    }

    if (instance->swipe_begin.link.prev)
    {
        wl_list_remove(&instance->swipe_begin.link);
    }

    if (instance->swipe_update.link.prev)
    {
        wl_list_remove(&instance->swipe_update.link);
    }

    if (instance->swipe_end.link.prev)
    {
        wl_list_remove(&instance->swipe_end.link);
    }

    if (instance->pinch_begin.link.prev)
    {
        wl_list_remove(&instance->pinch_begin.link);
    }

    if (instance->pinch_update.link.prev)
    {
        wl_list_remove(&instance->pinch_update.link);
    }

    if (instance->pinch_end.link.prev)
    {
        wl_list_remove(&instance->pinch_end.link);
    }

    if (instance->hold_begin.link.prev)
    {
        wl_list_remove(&instance->hold_begin.link);
    }

    if (instance->hold_end.link.prev)
    {
        wl_list_remove(&instance->hold_end.link);
    }

    if (instance->cursor_touch_down.link.prev)
    {
        wl_list_remove(&instance->cursor_touch_down.link);
    }

    if (instance->cursor_touch_up.link.prev)
    {
        wl_list_remove(&instance->cursor_touch_up.link);
    }

    if (instance->cursor_touch_motion.link.prev)
    {
        wl_list_remove(&instance->cursor_touch_motion.link);
    }

    if (instance->cursor_touch_frame.link.prev)
    {
        wl_list_remove(&instance->cursor_touch_frame.link);
    }

    if (instance->cursor_touch_cancel.link.prev)
    {
        wl_list_remove(&instance->cursor_touch_cancel.link);
    }

    sparrow_pointer_constraints_deactivate(instance);
    if (instance->new_pointer_constraint.link.prev)
    {
        wl_list_remove(&instance->new_pointer_constraint.link);
    }

    instance->input.scrolls.clear();
    instance->input.motions.clear();

    if (instance->new_input.link.prev)
    {
        wl_list_remove(&instance->new_input.link);
    }

    if (instance->cursor_shape_mgr != nullptr)
    {
        if (instance->request_set_cursor_shape.link.prev)
        {
            wl_list_remove(&instance->request_set_cursor_shape.link);
        }

        instance->cursor_shape_mgr = nullptr;
    }

    if (instance->cursor_mgr != nullptr)
    {
        wlr_xcursor_manager_destroy(instance->cursor_mgr);
        instance->cursor_mgr = nullptr;
    }

    if (instance->cursor != nullptr)
    {
        wlr_cursor_destroy(instance->cursor);
        instance->cursor = nullptr;
    }
}

bool flutter_mouse_button_to_linux(int64_t flutter_button,
    uint32_t *linux_button_out)
{
    switch (flutter_button)
    {
      case kFlutterPointerButtonMousePrimary:
        *linux_button_out = BTN_LEFT;
        return true;

      case kFlutterPointerButtonMouseSecondary:
        *linux_button_out = BTN_RIGHT;
        return true;

      case kFlutterPointerButtonMouseMiddle:
        *linux_button_out = BTN_MIDDLE;
        return true;

      case kFlutterPointerButtonMouseBack:
        *linux_button_out = BTN_SIDE;
        return true;

      case kFlutterPointerButtonMouseForward:
        *linux_button_out = BTN_EXTRA;
        return true;

      case (1LL << 5):
        *linux_button_out = BTN_TASK;
        return true;

      default:
        for (int bit = 0; bit < 16; bit++)
        {
            if (flutter_button == (1LL << bit))
            {
                if (bit < 8)
                {
                    *linux_button_out = BTN_MOUSE + bit;
                } else
                {
                    *linux_button_out = BTN_MISC + (bit - 8);
                }

                return true;
            }
        }

        return false;
    }
}

static std::mutex button_state_mutex;
static std::unordered_map<uint32_t, int64_t> surface_button_states;

int64_t get_surface_buttons(uint32_t surface_handle)
{
    std::scoped_lock lock(button_state_mutex);
    auto it = surface_button_states.find(surface_handle);
    return (it != surface_button_states.end()) ? it->second : 0;
}

void set_surface_buttons(uint32_t surface_handle, int64_t buttons)
{
    std::scoped_lock lock(button_state_mutex);
    if (buttons == 0)
    {
        surface_button_states.erase(surface_handle);
    } else
    {
        surface_button_states[surface_handle] = buttons;
    }
}

void sparrow_clear_surface_buttons(uint32_t surface_handle)
{
    std::scoped_lock lock(button_state_mutex);
    surface_button_states.erase(surface_handle);
}

std::optional<PointerDeviceKind> to_device_kind(uint8_t n)
{
    switch (n)
    {
      case 0:
        return PointerDeviceKind::touch;

      case 1:
        return PointerDeviceKind::mouse;

      case 2:
        return PointerDeviceKind::stylus;

      case 3:
        return PointerDeviceKind::invertedStylus;

      case 4:
        return PointerDeviceKind::trackpad;

      case 5:
        return PointerDeviceKind::unknown;

      default:
        return std::nullopt;
    }
}
