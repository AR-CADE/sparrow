#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <cstdint>
#include <string>
#include <functional>
#include <unordered_map>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

#include "flutter_embedder.h"

class WaylandWindow;

class InputManager
{
  public:
    explicit InputManager(WaylandWindow *window);
    ~InputManager();

    bool init();
    void shutdown();
    void bind_seat(struct wl_seat *seat);
    void set_engine(FlutterEngine engine);

    // Callbacks for Wayland seat capabilities
    void handle_seat_capabilities(struct wl_seat *seat, uint32_t capabilities);
    void handle_seat_name(struct wl_seat *seat, const char *name);

    // Pointer event handlers
    void handle_pointer_enter(uint32_t serial, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy);
    void handle_pointer_leave(uint32_t serial, struct wl_surface *surface);
    void handle_pointer_motion(uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
    void handle_pointer_button(uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
    void handle_pointer_axis(uint32_t time, uint32_t axis, wl_fixed_t value);
    void handle_pointer_axis_source(uint32_t axis_source);
    void handle_pointer_axis_stop(uint32_t time, uint32_t axis);
    void handle_pointer_axis_discrete(uint32_t axis, int32_t discrete);
    void handle_pointer_axis_value120(uint32_t axis, int32_t value120);
    void handle_pointer_frame();

    // Keyboard event handlers
    void handle_keyboard_keymap(uint32_t format, int32_t fd, uint32_t size);
    void handle_keyboard_enter(uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
    void handle_keyboard_leave(uint32_t serial, struct wl_surface *surface);
    void handle_keyboard_key(uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    void handle_keyboard_modifiers(uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
        uint32_t mods_locked, uint32_t group);
    void handle_keyboard_repeat_info(int32_t rate, int32_t delay);
    void handle_repeat_timer();
    int get_repeat_timer_fd() const
    {
        return repeat_timer_fd_;
    }

    // Touch event handlers
    void handle_touch_down(uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id,
        wl_fixed_t x, wl_fixed_t y);
    void handle_touch_up(uint32_t serial, uint32_t time, int32_t id);
    void handle_touch_motion(uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y);
    void handle_touch_frame();
    void handle_touch_cancel();

    // Hook for keyboard key events (text input & shortcuts)
    std::function<void(xkb_keysym_t keysym, uint32_t unicode, bool pressed)> on_key_event;

    // Cursor management
    void set_cursor(const std::string & kind);
    void apply_current_cursor();

    // Modifiers state
    bool is_ctrl_active() const;
    bool is_shift_active() const;

  private:
    void ensure_pointer_added(double x, double y);
    void send_pointer_event(FlutterPointerPhase phase, double x, double y, int64_t buttons,
        FlutterPointerSignalKind signal_kind = kFlutterPointerSignalKindNone, double scroll_delta_x = 0.0,
        double scroll_delta_y = 0.0);
    void send_touch_event(FlutterPointerPhase phase, int32_t device_id, double x, double y);

    WaylandWindow *window_ = nullptr;
    FlutterEngine engine_  = nullptr;

    struct wl_seat *seat_ = nullptr;
    bool seat_bound_ = false;

    struct wl_pointer *pointer_   = nullptr;
    struct wl_keyboard *keyboard_ = nullptr;
    struct wl_touch *touch_ = nullptr;

    struct wl_cursor_theme *cursor_theme_ = nullptr;
    struct wl_surface *cursor_surface_    = nullptr;
    uint32_t last_enter_serial_ = 0;
    std::string current_cursor_kind_ = "basic";
    bool pointer_added_ = false;

    double pointer_x_ = 0.0;
    double pointer_y_ = 0.0;
    int64_t pointer_buttons_ = 0;

    uint32_t current_axis_source_ = 0;
    bool pan_started_ = false;
    double pan_x_     = 0.0;
    double pan_y_     = 0.0;

    struct TouchPoint
    {
        double x = 0.0;
        double y = 0.0;
    };

    std::unordered_map<int32_t, TouchPoint> touch_points_;
    double last_touch_x_ = 0.0;
    double last_touch_y_ = 0.0;

    void start_repeat(uint32_t keycode, xkb_keysym_t sym, uint32_t unicode);
    void stop_repeat();

    int repeat_timer_fd_  = -1;
    int32_t repeat_rate_  = 25;
    int32_t repeat_delay_ = 300;
    bool repeat_active_   = false;
    uint32_t repeat_keycode_ = 0;
    xkb_keysym_t repeat_sym_ = XKB_KEY_NoSymbol;
    uint32_t repeat_unicode_ = 0;

    struct xkb_context *xkb_context_ = nullptr;
    struct xkb_keymap *xkb_keymap_   = nullptr;
    struct xkb_state *xkb_state_     = nullptr;
    struct xkb_compose_table *compose_table_ = nullptr;
    struct xkb_compose_state *compose_state_ = nullptr;
};

#endif // INPUT_MANAGER_HPP
