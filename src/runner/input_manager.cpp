#include "input_manager.hpp"
#include "wayland_window.hpp"
#include "cursor-shape-v1-client-protocol.h"

#include <cstdio>
#include <cstring>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-cursor.h>

// --- Static Wayland Seat Listeners ---

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    auto *self = static_cast<InputManager*>(data);
    self->handle_seat_capabilities(seat, capabilities);
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name)
{
    auto *self = static_cast<InputManager*>(data);
    self->handle_seat_name(seat, name);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

// --- Pointer Listeners ---

static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_enter(serial, surface, sx, sy);
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_leave(serial, surface);
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx,
    wl_fixed_t sy)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_motion(time, sx, sy);
}

static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time,
    uint32_t button, uint32_t state)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_button(serial, time, button, state);
}

static void pointer_handle_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
    wl_fixed_t value)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_axis(time, axis, value);
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_axis_source(axis_source);
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_axis_stop(time, axis);
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
    int32_t discrete)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_axis_discrete(axis, discrete);
}

static void pointer_handle_frame(void *data, struct wl_pointer *pointer)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_frame();
}

static void pointer_handle_axis_value120(void *data,
    struct wl_pointer *pointer,
    uint32_t axis,
    int32_t value120)
{
    (void)pointer;
    auto *self = static_cast<InputManager*>(data);
    self->handle_pointer_axis_value120(axis, value120);
}

static const struct wl_pointer_listener pointer_listener = {
    .enter  = pointer_handle_enter,
    .leave  = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis   = pointer_handle_axis,
    .frame  = pointer_handle_frame,
    .axis_source   = pointer_handle_axis_source,
    .axis_stop     = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
    .axis_value120 = pointer_handle_axis_value120,
};

// --- Keyboard Listeners ---

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
    uint32_t size)
{
    (void)keyboard;
    auto *self = static_cast<InputManager*>(data);
    self->handle_keyboard_keymap(format, fd, size);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
    struct wl_surface *surface, struct wl_array *keys)
{
    (void)keyboard;
    auto *self = static_cast<InputManager*>(data);
    self->handle_keyboard_enter(serial, surface, keys);
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
    struct wl_surface *surface)
{
    (void)keyboard;
    auto *self = static_cast<InputManager*>(data);
    self->handle_keyboard_leave(serial, surface);
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
    uint32_t key, uint32_t state)
{
    (void)keyboard;
    auto *self = static_cast<InputManager*>(data);
    self->handle_keyboard_key(serial, time, key, state);
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
    uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    (void)keyboard;
    auto *self = static_cast<InputManager*>(data);
    self->handle_keyboard_modifiers(serial, mods_depressed, mods_latched, mods_locked, group);
}

static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay)
{
    (void)keyboard;
    auto *self = static_cast<InputManager*>(data);
    self->handle_keyboard_repeat_info(rate, delay);
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter  = keyboard_handle_enter,
    .leave  = keyboard_handle_leave,
    .key    = keyboard_handle_key,
    .modifiers   = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

// --- Touch Listeners ---

static void touch_handle_down(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
    struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    (void)touch;
    auto *self = static_cast<InputManager*>(data);
    self->handle_touch_down(serial, time, surface, id, x, y);
}

static void touch_handle_up(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, int32_t id)
{
    (void)touch;
    auto *self = static_cast<InputManager*>(data);
    self->handle_touch_up(serial, time, id);
}

static void touch_handle_motion(void *data, struct wl_touch *touch, uint32_t time, int32_t id, wl_fixed_t x,
    wl_fixed_t y)
{
    (void)touch;
    auto *self = static_cast<InputManager*>(data);
    self->handle_touch_motion(time, id, x, y);
}

static void touch_handle_frame(void *data, struct wl_touch *touch)
{
    (void)touch;
    auto *self = static_cast<InputManager*>(data);
    self->handle_touch_frame();
}

static void touch_handle_cancel(void *data, struct wl_touch *touch)
{
    (void)touch;
    auto *self = static_cast<InputManager*>(data);
    self->handle_touch_cancel();
}

static const struct wl_touch_listener touch_listener = {
    .down   = touch_handle_down,
    .up     = touch_handle_up,
    .motion = touch_handle_motion,
    .frame  = touch_handle_frame,
    .cancel = touch_handle_cancel,
    .shape  = [] (void*, struct wl_touch*, int32_t, wl_fixed_t, wl_fixed_t) {},
    .orientation = [] (void*, struct wl_touch*, int32_t, wl_fixed_t) {},
};

static const char * flutter_cursor_to_xcursor(const char *flutter_kind)
{
    if (flutter_kind == nullptr)
    {
        return "left_ptr";
    }

    if (strcmp(flutter_kind, "basic") == 0)
    {
        return "left_ptr";
    }

    if (strcmp(flutter_kind, "none") == 0)
    {
        return nullptr;
    }

    if ((strcmp(flutter_kind, "click") == 0) || (strcmp(flutter_kind, "pointer") == 0))
    {
        return "pointer";
    }

    if (strcmp(flutter_kind, "text") == 0)
    {
        return "xterm";
    }

    if (strcmp(flutter_kind, "verticalText") == 0)
    {
        return "vertical-text";
    }

    if (strcmp(flutter_kind, "cell") == 0)
    {
        return "cell";
    }

    if (strcmp(flutter_kind, "crosshair") == 0)
    {
        return "crosshair";
    }

    if (strcmp(flutter_kind, "move") == 0)
    {
        return "move";
    }

    if (strcmp(flutter_kind, "grab") == 0)
    {
        return "grab";
    }

    if (strcmp(flutter_kind, "grabbing") == 0)
    {
        return "grabbing";
    }

    if (strcmp(flutter_kind, "allScroll") == 0)
    {
        return "all-scroll";
    }

    if ((strcmp(flutter_kind, "resizeLeft") == 0) || (strcmp(flutter_kind, "resizeRight") == 0) ||
        (strcmp(flutter_kind, "resizeLeftRight") == 0))
    {
        return "ew-resize";
    }

    if ((strcmp(flutter_kind, "resizeUp") == 0) || (strcmp(flutter_kind, "resizeDown") == 0) ||
        (strcmp(flutter_kind, "resizeUpDown") == 0))
    {
        return "ns-resize";
    }

    if ((strcmp(flutter_kind, "resizeUpLeft") == 0) || (strcmp(flutter_kind, "resizeDownRight") == 0))
    {
        return "nwse-resize";
    }

    if ((strcmp(flutter_kind, "resizeUpRight") == 0) || (strcmp(flutter_kind, "resizeDownLeft") == 0))
    {
        return "nesw-resize";
    }

    if (strcmp(flutter_kind, "resizeColumn") == 0)
    {
        return "col-resize";
    }

    if (strcmp(flutter_kind, "resizeRow") == 0)
    {
        return "row-resize";
    }

    if ((strcmp(flutter_kind, "notAllowed") == 0) || (strcmp(flutter_kind, "forbidden") == 0))
    {
        return "not-allowed";
    }

    if (strcmp(flutter_kind, "wait") == 0)
    {
        return "wait";
    }

    if (strcmp(flutter_kind, "progress") == 0)
    {
        return "progress";
    }

    if (strcmp(flutter_kind, "help") == 0)
    {
        return "help";
    }

    if (strcmp(flutter_kind, "copy") == 0)
    {
        return "copy";
    }

    if (strcmp(flutter_kind, "alias") == 0)
    {
        return "alias";
    }

    if (strcmp(flutter_kind, "noDrop") == 0)
    {
        return "no-drop";
    }

    if (strcmp(flutter_kind, "zoomIn") == 0)
    {
        return "zoom-in";
    }

    if (strcmp(flutter_kind, "zoomOut") == 0)
    {
        return "zoom-out";
    }

    return "left_ptr";
}

static uint32_t flutter_cursor_to_shape(const std::string & kind)
{
    if ((kind == "click") || (kind == "pointer"))
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    }

    if (kind == "text")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
    }

    if (kind == "verticalText")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT;
    }

    if (kind == "cell")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CELL;
    }

    if (kind == "crosshair")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
    }

    if (kind == "move")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE;
    }

    if (kind == "grab")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB;
    }

    if (kind == "grabbing")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING;
    }

    if (kind == "allScroll")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL;
    }

    if ((kind == "resizeLeft") || (kind == "resizeRight") || (kind == "resizeLeftRight"))
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE;
    }

    if ((kind == "resizeUp") || (kind == "resizeDown") || (kind == "resizeUpDown"))
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE;
    }

    if ((kind == "resizeUpLeft") || (kind == "resizeDownRight"))
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE;
    }

    if ((kind == "resizeUpRight") || (kind == "resizeDownLeft"))
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE;
    }

    if (kind == "resizeColumn")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE;
    }

    if (kind == "resizeRow")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE;
    }

    if ((kind == "notAllowed") || (kind == "forbidden"))
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED;
    }

    if (kind == "wait")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT;
    }

    if (kind == "progress")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS;
    }

    if (kind == "help")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_HELP;
    }

    if (kind == "copy")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COPY;
    }

    if (kind == "alias")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALIAS;
    }

    if (kind == "noDrop")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP;
    }

    if (kind == "zoomIn")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_IN;
    }

    if (kind == "zoomOut")
    {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT;
    }

    return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
}

// --- InputManager Implementation ---

InputManager::InputManager(WaylandWindow *window) :
    window_(window)
{
    xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_context_)
    {
        fprintf(stderr, "[sparrow-app-runner] Failed to create XKB context\n");
    }
}

InputManager::~InputManager()
{
    shutdown();
}

void InputManager::bind_seat(struct wl_seat *seat)
{
    if (!seat || seat_bound_)
    {
        return;
    }

    seat_ = seat;
    seat_bound_ = true;
    wl_seat_add_listener(seat_, &seat_listener, this);
}

bool InputManager::init()
{
    if (!seat_bound_ && window_ && window_->get_seat())
    {
        bind_seat(window_->get_seat());
    }

    if (!xkb_context_)
    {
        xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    }

    if (window_ && window_->get_shm())
    {
        cursor_theme_ = wl_cursor_theme_load(nullptr, 24, window_->get_shm());
    }

    if (window_ && window_->get_compositor())
    {
        cursor_surface_ = wl_compositor_create_surface(window_->get_compositor());
    }

    repeat_timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (window_ && (repeat_timer_fd_ >= 0))
    {
        window_->set_repeat_timer(repeat_timer_fd_, [this] ()
        {
            handle_repeat_timer();
        });
    }

    return true;
}

void InputManager::shutdown()
{
    if (seat_bound_)
    {
        seat_ = nullptr;
        seat_bound_ = false;
    }

    if (cursor_surface_)
    {
        wl_surface_destroy(cursor_surface_);
        cursor_surface_ = nullptr;
    }

    if (cursor_theme_)
    {
        wl_cursor_theme_destroy(cursor_theme_);
        cursor_theme_ = nullptr;
    }

    if (pointer_)
    {
        wl_pointer_release(pointer_);
        pointer_ = nullptr;
    }

    if (keyboard_)
    {
        wl_keyboard_release(keyboard_);
        keyboard_ = nullptr;
    }

    if (touch_)
    {
        wl_touch_release(touch_);
        touch_ = nullptr;
    }

    if (compose_state_)
    {
        xkb_compose_state_unref(compose_state_);
        compose_state_ = nullptr;
    }

    if (compose_table_)
    {
        xkb_compose_table_unref(compose_table_);
        compose_table_ = nullptr;
    }

    if (xkb_state_)
    {
        xkb_state_unref(xkb_state_);
        xkb_state_ = nullptr;
    }

    if (xkb_keymap_)
    {
        xkb_keymap_unref(xkb_keymap_);
        xkb_keymap_ = nullptr;
    }

    if (xkb_context_)
    {
        xkb_context_unref(xkb_context_);
        xkb_context_ = nullptr;
    }

    if (pan_started_ && engine_)
    {
        const double pr = (window_ && (window_->get_pixel_ratio() > 0.0)) ?
            window_->get_pixel_ratio() : 1.0;
        uint64_t ts_us = FlutterEngineGetCurrentTime() / 1000ULL;
        FlutterPointerEvent end_ev = {};
        end_ev.struct_size = sizeof(FlutterPointerEvent);
        end_ev.phase     = kPanZoomEnd;
        end_ev.timestamp = ts_us;
        end_ev.x     = pointer_x_ * pr;
        end_ev.y     = pointer_y_ * pr;
        end_ev.pan_x = pan_x_;
        end_ev.pan_y = pan_y_;
        end_ev.scale = 0.0;
        end_ev.rotation = 0.0;
        end_ev.device   = 1;
        end_ev.device_kind = kFlutterPointerDeviceKindTrackpad;
        end_ev.buttons     = 0;
        FlutterEngineSendPointerEvent(engine_, &end_ev, 1);
        pan_started_ = false;
        pan_x_ = 0.0;
        pan_y_ = 0.0;
    }

    touch_points_.clear();
    current_axis_source_ = 0;
    pointer_added_ = false;

    stop_repeat();
    if (window_)
    {
        window_->set_repeat_timer(-1, nullptr);
    }

    if (repeat_timer_fd_ >= 0)
    {
        close(repeat_timer_fd_);
        repeat_timer_fd_ = -1;
    }
}

void InputManager::set_engine(FlutterEngine engine)
{
    engine_ = engine;
    if (engine_ && (pointer_ != nullptr))
    {
        ensure_pointer_added(pointer_x_, pointer_y_);
    }
}

void InputManager::set_cursor(const std::string & kind)
{
    current_cursor_kind_ = kind;
    apply_current_cursor();
}

void InputManager::apply_current_cursor()
{
    if (!pointer_ || (last_enter_serial_ == 0))
    {
        return;
    }

    if (current_cursor_kind_ == "none")
    {
        wl_pointer_set_cursor(pointer_, last_enter_serial_, nullptr, 0, 0);
        return;
    }

    if (window_ && window_->get_cursor_shape_manager())
    {
        struct wp_cursor_shape_device_v1 *device =
            wp_cursor_shape_manager_v1_get_pointer(window_->get_cursor_shape_manager(), pointer_);
        if (device)
        {
            uint32_t shape = flutter_cursor_to_shape(current_cursor_kind_);
            wp_cursor_shape_device_v1_set_shape(device, last_enter_serial_, shape);
            wp_cursor_shape_device_v1_destroy(device);
            return;
        }
    }

    if (cursor_theme_ && cursor_surface_)
    {
        const char *xcursor_name = flutter_cursor_to_xcursor(current_cursor_kind_.c_str());
        struct wl_cursor *cursor = wl_cursor_theme_get_cursor(cursor_theme_, xcursor_name);
        if (!cursor)
        {
            cursor = wl_cursor_theme_get_cursor(cursor_theme_, "left_ptr");
        }

        if (!cursor)
        {
            cursor = wl_cursor_theme_get_cursor(cursor_theme_, "default");
        }

        if (cursor && (cursor->image_count > 0))
        {
            struct wl_cursor_image *img = cursor->images[0];
            struct wl_buffer *buf = wl_cursor_image_get_buffer(img);
            if (buf)
            {
                wl_surface_attach(cursor_surface_, buf, 0, 0);
                wl_surface_damage(cursor_surface_, 0, 0, img->width, img->height);
                wl_surface_commit(cursor_surface_);
                wl_pointer_set_cursor(pointer_, last_enter_serial_, cursor_surface_, img->hotspot_x,
                    img->hotspot_y);
            }
        }
    }
}

void InputManager::handle_seat_capabilities(struct wl_seat *seat, uint32_t capabilities)
{
    printf("[sparrow-app-runner] Seat capabilities: pointer=%d keyboard=%d touch=%d\n",
        (capabilities & WL_SEAT_CAPABILITY_POINTER) ? 1 : 0,
        (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) ? 1 : 0,
        (capabilities & WL_SEAT_CAPABILITY_TOUCH) ? 1 : 0);
    fflush(stdout);

    // Pointer
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !pointer_)
    {
        pointer_ = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer_, &pointer_listener, this);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && pointer_)
    {
        wl_pointer_release(pointer_);
        pointer_ = nullptr;
    }

    // Keyboard
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard_)
    {
        keyboard_ = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(keyboard_, &keyboard_listener, this);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard_)
    {
        wl_keyboard_release(keyboard_);
        keyboard_ = nullptr;
    }

    // Touch
    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !touch_)
    {
        touch_ = wl_seat_get_touch(seat);
        wl_touch_add_listener(touch_, &touch_listener, this);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_TOUCH) && touch_)
    {
        wl_touch_release(touch_);
        touch_ = nullptr;
    }
}

void InputManager::handle_seat_name(struct wl_seat *seat, const char *name)
{
    (void)seat;
    (void)name;
}

void InputManager::ensure_pointer_added(double x, double y)
{
    if (pointer_added_ || !engine_)
    {
        return;
    }

    const double pr = (window_ && (window_->get_pixel_ratio() > 0.0)) ?
        window_->get_pixel_ratio() : 1.0;

    FlutterPointerEvent event = {};
    event.struct_size = sizeof(FlutterPointerEvent);
    event.phase     = kAdd;
    event.timestamp = FlutterEngineGetCurrentTime() / 1000;
    event.x = x * pr;
    event.y = y * pr;
    event.device = 0;
    event.signal_kind = kFlutterPointerSignalKindNone;
    event.device_kind = kFlutterPointerDeviceKindMouse;
    event.buttons     = 0;

    FlutterEngineResult res = FlutterEngineSendPointerEvent(engine_, &event, 1);
    if (res == kSuccess)
    {
        pointer_added_ = true;
    }
}

void InputManager::send_pointer_event(FlutterPointerPhase phase,
    double x,
    double y,
    int64_t buttons,
    FlutterPointerSignalKind signal_kind,
    double scroll_delta_x,
    double scroll_delta_y)
{
    if (!engine_)
    {
        return;
    }

    const double pr = (window_ && (window_->get_pixel_ratio() > 0.0)) ?
        window_->get_pixel_ratio() : 1.0;

    FlutterPointerEvent event = {};
    event.struct_size = sizeof(FlutterPointerEvent);
    event.phase     = phase;
    event.timestamp = FlutterEngineGetCurrentTime() / 1000;
    event.x = x * pr;
    event.y = y * pr;
    event.device = 0;
    event.signal_kind    = signal_kind;
    event.scroll_delta_x = scroll_delta_x * pr;
    event.scroll_delta_y = scroll_delta_y * pr;
    event.device_kind    = kFlutterPointerDeviceKindMouse;
    event.buttons = buttons;

    FlutterEngineSendPointerEvent(engine_, &event, 1);
}

void InputManager::handle_pointer_enter(uint32_t serial, struct wl_surface *surface, wl_fixed_t sx,
    wl_fixed_t sy)
{
    (void)surface;
    last_enter_serial_ = serial;
    if (window_)
    {
        window_->set_last_serial(serial);
    }

    pointer_x_ = wl_fixed_to_double(sx);
    pointer_y_ = wl_fixed_to_double(sy);

    printf("[sparrow-app-runner] Pointer enter: x=%.1f y=%.1f serial=%u\n",
        pointer_x_, pointer_y_, serial);
    fflush(stdout);

    apply_current_cursor();
    ensure_pointer_added(pointer_x_, pointer_y_);
    send_pointer_event(kHover, pointer_x_, pointer_y_, pointer_buttons_);
}

void InputManager::handle_pointer_leave(uint32_t serial, struct wl_surface *surface)
{
    (void)serial;
    (void)surface;
    if (pointer_added_ && engine_)
    {
        send_pointer_event(kRemove, pointer_x_, pointer_y_, 0);
        pointer_added_ = false;
    }
}

void InputManager::handle_pointer_motion(uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)time;
    pointer_x_ = wl_fixed_to_double(sx);
    pointer_y_ = wl_fixed_to_double(sy);

    ensure_pointer_added(pointer_x_, pointer_y_);
    FlutterPointerPhase phase = (pointer_buttons_ != 0) ? kMove : kHover;
    send_pointer_event(phase, pointer_x_, pointer_y_, pointer_buttons_);
}

void InputManager::handle_pointer_button(uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    if (window_)
    {
        window_->set_last_serial(serial);
    }

    (void)time;

    ensure_pointer_added(pointer_x_, pointer_y_);

    int64_t flutter_button = 0;
    switch (button)
    {
      case BTN_LEFT:
        flutter_button = kFlutterPointerButtonMousePrimary;
        break;

      case BTN_RIGHT:
        flutter_button = kFlutterPointerButtonMouseSecondary;
        break;

      case BTN_MIDDLE:
        flutter_button = kFlutterPointerButtonMouseMiddle;
        break;

      case BTN_SIDE:
        flutter_button = kFlutterPointerButtonMouseBack;
        break;

      case BTN_EXTRA:
        flutter_button = kFlutterPointerButtonMouseForward;
        break;

      default:
        flutter_button = (1ULL << (button - BTN_MOUSE));
        break;
    }

    FlutterPointerPhase phase;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        bool was_down = (pointer_buttons_ != 0);
        pointer_buttons_ |= flutter_button;
        phase = was_down ? kMove : kDown;
    } else
    {
        pointer_buttons_ &= ~flutter_button;
        phase = (pointer_buttons_ == 0) ? kUp : kMove;
    }

    printf("[sparrow-app-runner] Pointer button: button=0x%x (%s) state=%u phase=%d buttons=0x%lx\n",
        button, (button == BTN_LEFT) ? "BTN_LEFT" : "OTHER", state, phase, (unsigned long)pointer_buttons_);
    fflush(stdout);

    send_pointer_event(phase, pointer_x_, pointer_y_, pointer_buttons_);
}

void InputManager::handle_pointer_axis(uint32_t time, uint32_t axis, wl_fixed_t value)
{
    bool is_touchpad = (current_axis_source_ == WL_POINTER_AXIS_SOURCE_FINGER) ||
        (current_axis_source_ == WL_POINTER_AXIS_SOURCE_CONTINUOUS);
    double delta    = wl_fixed_to_double(value);
    const double pr = (window_ && (window_->get_pixel_ratio() > 0.0)) ?
        window_->get_pixel_ratio() : 1.0;
    uint64_t ts_us = (time != 0) ? ((uint64_t)time * 1000ULL) :
        (FlutterEngineGetCurrentTime() / 1000ULL);

    if (is_touchpad)
    {
        // Touchpad two-finger pan:
        // In GTK (fl_scrolling_manager.cc), Wayland delta is scaled by Chromium kScrollOffsetMultiplier
        // (53.0 / 10.0 = 5.3) and inverted to match natural gesture dragging.
        const double kTrackpadMultiplier = 5.3;
        double delta_x = 0.0;
        double delta_y = 0.0;
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            delta_y = -delta * kTrackpadMultiplier;
        } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        {
            delta_x = -delta * kTrackpadMultiplier;
        }

        if (!pan_started_)
        {
            pan_x_ = 0.0;
            pan_y_ = 0.0;
            FlutterPointerEvent start_ev = {};
            start_ev.struct_size = sizeof(FlutterPointerEvent);
            start_ev.phase     = kPanZoomStart;
            start_ev.timestamp = ts_us;
            start_ev.x     = pointer_x_ * pr;
            start_ev.y     = pointer_y_ * pr;
            start_ev.pan_x = 0.0;
            start_ev.pan_y = 0.0;
            start_ev.scale = 0.0;
            start_ev.rotation = 0.0;
            start_ev.device   = 1;
            start_ev.device_kind = kFlutterPointerDeviceKindTrackpad;
            start_ev.buttons     = 0;
            FlutterEngineSendPointerEvent(engine_, &start_ev, 1);
            pan_started_ = true;
        }

        pan_x_ += delta_x * pr;
        pan_y_ += delta_y * pr;

        FlutterPointerEvent update_ev = {};
        update_ev.struct_size = sizeof(FlutterPointerEvent);
        update_ev.phase     = kPanZoomUpdate;
        update_ev.timestamp = ts_us;
        update_ev.x     = pointer_x_ * pr;
        update_ev.y     = pointer_y_ * pr;
        update_ev.pan_x = pan_x_;
        update_ev.pan_y = pan_y_;
        update_ev.scale = 1.0;
        update_ev.rotation = 0.0;
        update_ev.device   = 1;
        update_ev.device_kind = kFlutterPointerDeviceKindTrackpad;
        update_ev.buttons     = 0;
        FlutterEngineSendPointerEvent(engine_, &update_ev, 1);
    } else
    {
        // Mouse wheel: send discrete scroll events on device 0 (Mouse)
        double scale    = 2.5;
        double scroll_x = 0.0;
        double scroll_y = 0.0;
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            scroll_y = delta * scale;
        } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        {
            scroll_x = delta * scale;
        }

        send_pointer_event(kHover, pointer_x_, pointer_y_, pointer_buttons_,
            kFlutterPointerSignalKindScroll, scroll_x, scroll_y);
    }
}

void InputManager::handle_pointer_axis_source(uint32_t axis_source)
{
    current_axis_source_ = axis_source;
}

void InputManager::handle_pointer_axis_stop(uint32_t time, uint32_t axis)
{
    (void)axis;
    if (pan_started_)
    {
        const double pr = (window_ && (window_->get_pixel_ratio() > 0.0)) ?
            window_->get_pixel_ratio() : 1.0;
        uint64_t ts_us = (time != 0) ? ((uint64_t)time * 1000ULL) :
            (FlutterEngineGetCurrentTime() / 1000ULL);

        FlutterPointerEvent end_ev = {};
        end_ev.struct_size = sizeof(FlutterPointerEvent);
        end_ev.phase     = kPanZoomEnd;
        end_ev.timestamp = ts_us;
        end_ev.x     = pointer_x_ * pr;
        end_ev.y     = pointer_y_ * pr;
        end_ev.pan_x = pan_x_;
        end_ev.pan_y = pan_y_;
        end_ev.scale = 0.0;
        end_ev.rotation = 0.0;
        end_ev.device   = 1;
        end_ev.device_kind = kFlutterPointerDeviceKindTrackpad;
        end_ev.buttons     = 0;
        FlutterEngineSendPointerEvent(engine_, &end_ev, 1);

        pan_started_ = false;
        pan_x_ = 0.0;
        pan_y_ = 0.0;
    } else
    {
        send_pointer_event(kHover, pointer_x_, pointer_y_, pointer_buttons_,
            kFlutterPointerSignalKindScrollInertiaCancel, 0.0, 0.0);
    }
}

void InputManager::handle_pointer_axis_discrete(uint32_t axis, int32_t discrete)
{
    (void)axis;
    (void)discrete;
}

void InputManager::handle_pointer_axis_value120(uint32_t axis, int32_t value120)
{
    (void)axis;
    (void)value120;
}

void InputManager::handle_pointer_frame()
{
}

// --- Keyboard Handling ---

void InputManager::handle_keyboard_keymap(uint32_t format, int32_t fd, uint32_t size)
{
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
        close(fd);
        return;
    }

    char *map_str = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
    if (map_str == MAP_FAILED)
    {
        close(fd);
        return;
    }

    if (!xkb_context_)
    {
        xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    }

    if (!xkb_context_)
    {
        munmap(map_str, size);
        close(fd);
        return;
    }

    if (xkb_keymap_)
    {
        xkb_keymap_unref(xkb_keymap_);
    }

    if (xkb_state_)
    {
        xkb_state_unref(xkb_state_);
    }

    xkb_keymap_ = xkb_keymap_new_from_string(xkb_context_, map_str,
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);
    close(fd);

    if (!xkb_keymap_)
    {
        fprintf(stderr, "[sparrow-app-runner] Failed to compile XKB keymap\n");
        return;
    }

    xkb_state_ = xkb_state_new(xkb_keymap_);
}

void InputManager::handle_keyboard_enter(uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    if (window_)
    {
        window_->set_last_serial(serial);
    }

    (void)surface;
    (void)keys;
}

void InputManager::handle_keyboard_leave(uint32_t serial, struct wl_surface *surface)
{
    (void)serial;
    (void)surface;
    stop_repeat();
}

void InputManager::handle_keyboard_key(uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    if (window_)
    {
        window_->set_last_serial(serial);
    }

    (void)time;

    if (!xkb_state_)
    {
        return;
    }

    uint32_t keycode = key + 8; // evdev offset
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state_, keycode);
    uint32_t unicode = xkb_state_key_get_utf32(xkb_state_, keycode);
    bool pressed     = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

    if (pressed)
    {
        start_repeat(keycode, sym, unicode);
    } else
    {
        if (repeat_active_ && (repeat_keycode_ == keycode))
        {
            stop_repeat();
        }
    }

    if (on_key_event)
    {
        on_key_event(sym, unicode, pressed);
    }
}

void InputManager::handle_keyboard_modifiers(uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
    uint32_t mods_locked, uint32_t group)
{
    (void)serial;
    if (xkb_state_)
    {
        xkb_state_update_mask(xkb_state_, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    }
}

void InputManager::handle_keyboard_repeat_info(int32_t rate, int32_t delay)
{
    repeat_rate_  = rate;
    repeat_delay_ = delay;
    if ((repeat_rate_ <= 0) && repeat_active_)
    {
        stop_repeat();
    }
}

void InputManager::start_repeat(uint32_t keycode, xkb_keysym_t sym, uint32_t unicode)
{
    if ((repeat_rate_ <= 0) || (repeat_delay_ <= 0) || (repeat_timer_fd_ < 0))
    {
        return;
    }

    if (xkb_keymap_ && !xkb_keymap_key_repeats(xkb_keymap_, keycode))
    {
        return;
    }

    repeat_keycode_ = keycode;
    repeat_sym_     = sym;
    repeat_unicode_ = unicode;
    repeat_active_  = true;

    struct itimerspec its = {};
    its.it_value.tv_sec  = repeat_delay_ / 1000;
    its.it_value.tv_nsec = (repeat_delay_ % 1000) * 1000000ULL;
    int interval_ms = (repeat_rate_ > 0) ? (1000 / repeat_rate_) : 40;
    its.it_interval.tv_sec  = interval_ms / 1000;
    its.it_interval.tv_nsec = (interval_ms % 1000) * 1000000ULL;

    timerfd_settime(repeat_timer_fd_, 0, &its, nullptr);
}

void InputManager::stop_repeat()
{
    if (repeat_active_ && (repeat_timer_fd_ >= 0))
    {
        struct itimerspec its = {};
        timerfd_settime(repeat_timer_fd_, 0, &its, nullptr);
    }

    repeat_active_  = false;
    repeat_keycode_ = 0;
    repeat_sym_     = XKB_KEY_NoSymbol;
    repeat_unicode_ = 0;
}

void InputManager::handle_repeat_timer()
{
    if (repeat_timer_fd_ < 0)
    {
        return;
    }

    uint64_t expirations = 0;
    ssize_t s = read(repeat_timer_fd_, &expirations, sizeof(expirations));
    if ((s < 0) || (expirations == 0))
    {
        return;
    }

    if (repeat_active_ && on_key_event)
    {
        for (uint64_t i = 0; i < expirations; ++i)
        {
            on_key_event(repeat_sym_, repeat_unicode_, true);
        }
    }
}

// --- Touch Handling ---

void InputManager::send_touch_event(FlutterPointerPhase phase, int32_t device_id, double x, double y)
{
    if (!engine_)
    {
        return;
    }

    const double pr = (window_ && (window_->get_pixel_ratio() > 0.0)) ?
        window_->get_pixel_ratio() : 1.0;

    FlutterPointerEvent event = {};
    event.struct_size = sizeof(FlutterPointerEvent);
    event.phase     = phase;
    event.timestamp = FlutterEngineGetCurrentTime() / 1000;
    event.x = x * pr;
    event.y = y * pr;
    event.device = device_id;
    event.signal_kind = kFlutterPointerSignalKindNone;
    event.device_kind = kFlutterPointerDeviceKindTouch;
    event.buttons     = 0;

    FlutterEngineSendPointerEvent(engine_, &event, 1);
}

void InputManager::handle_touch_down(uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id,
    wl_fixed_t x, wl_fixed_t y)
{
    if (window_)
    {
        window_->set_last_serial(serial);
    }

    (void)time;
    (void)surface;
    double dx = wl_fixed_to_double(x);
    double dy = wl_fixed_to_double(y);
    touch_points_[id] = {dx, dy};
    last_touch_x_     = dx;
    last_touch_y_     = dy;

    send_touch_event(kDown, id, dx, dy);
}

void InputManager::handle_touch_up(uint32_t serial, uint32_t time, int32_t id)
{
    (void)serial;
    (void)time;
    double dx = last_touch_x_;
    double dy = last_touch_y_;
    auto it   = touch_points_.find(id);
    if (it != touch_points_.end())
    {
        dx = it->second.x;
        dy = it->second.y;
        touch_points_.erase(it);
    }

    send_touch_event(kUp, id, dx, dy);
}

void InputManager::handle_touch_motion(uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    (void)time;
    double dx = wl_fixed_to_double(x);
    double dy = wl_fixed_to_double(y);
    touch_points_[id] = {dx, dy};
    last_touch_x_     = dx;
    last_touch_y_     = dy;

    send_touch_event(kMove, id, dx, dy);
}

void InputManager::handle_touch_frame()
{
}

void InputManager::handle_touch_cancel()
{
    for (const auto & [id, pt] : touch_points_)
    {
        send_touch_event(kCancel, id, pt.x, pt.y);
    }

    touch_points_.clear();
}

bool InputManager::is_ctrl_active() const
{
    if (!xkb_state_)
    {
        return false;
    }

    return xkb_state_mod_name_is_active(xkb_state_, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
}

bool InputManager::is_shift_active() const
{
    if (!xkb_state_)
    {
        return false;
    }

    return xkb_state_mod_name_is_active(xkb_state_, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
}
