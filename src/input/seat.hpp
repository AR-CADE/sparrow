#ifndef SEAT_H
#define SEAT_H

#include <cstdint>

#include "flutter_embedder.h"
#include "touch.hpp"
#include <memory>
#include <optional>
#include <sparrow/nonstd/wlroots-full.hpp>
#include <vector>

enum class PointerDeviceKind
{
    /// A touch-based pointer device.
    ///
    /// The most common case is a touch screen.
    ///
    /// When the user is operating with a trackpad on iOS, clicking will also
    /// dispatch events with kind [touch] if
    /// `UIApplicationSupportsIndirectInputEvents` is not present in `Info.plist`
    /// or returns NO.
    ///
    /// See also:
    ///
    ///  *
    // [UIApplicationSupportsIndirectInputEvents](https://developer.apple.com/documentation/bundleresources/information_property_list/uiapplicationsupportsindirectinputevents?language=objc).
    touch,
    /// A mouse-based pointer device.
    ///
    /// The most common case is a mouse on the desktop or Web.
    ///
    /// When the user is operating with a trackpad on iOS, moving the pointing
    /// cursor will also dispatch events with kind [mouse], and clicking will
    /// dispatch events with kind [mouse] if
    /// `UIApplicationSupportsIndirectInputEvents` is not present in `Info.plist`
    /// or returns NO.
    ///
    /// See also:
    ///
    ///  *
    // [UIApplicationSupportsIndirectInputEvents](https://developer.apple.com/documentation/bundleresources/information_property_list/uiapplicationsupportsindirectinputevents?language=objc).
    mouse,
    /// A pointer device with a stylus.
    stylus,
    /// A pointer device with a stylus that has been inverted.
    invertedStylus,
    /// Gestures from a trackpad.
    ///
    /// A trackpad here is defined as a touch-based pointer device with an
    /// indirect surface (the user operates the screen by touching something that
    /// is not the screen).
    ///
    /// When the user makes zoom, pan, scroll or rotate gestures with a physical
    /// trackpad, supporting platforms dispatch events with kind [trackpad].
    ///
    /// Events with kind [trackpad] can only have a [PointerChange] of `add`,
    /// `remove`, and pan-zoom related values.
    ///
    /// Some platforms don't support (or don't fully support) trackpad
    /// gestures, and might convert trackpad gestures into fake pointer events
    /// that simulate dragging. These events typically have kind [touch] or
    /// [mouse] instead of [trackpad]. This includes (but is not limited to) Web,
    /// and iOS when `UIApplicationSupportsIndirectInputEvents` isn't present in
    /// `Info.plist` or returns NO.
    ///
    /// Moving the pointing cursor or clicking with a trackpad typically triggers
    /// [touch] or [mouse] events, but never triggers [trackpad] events.
    ///
    /// See also:
    ///
    ///  *
    // [UIApplicationSupportsIndirectInputEvents](https://developer.apple.com/documentation/bundleresources/information_property_list/uiapplicationsupportsindirectinputevents?language=objc).
    trackpad,
    /// An unknown pointer device.
    unknown,
};

struct pointer_point
{
    bool valid = false;
    int32_t id = 0;
    uint32_t event_mask = 0;
    double sx = 0, sy = 0;
    // double x, y;
    uint32_t time_msec = 0;
    int32_t pointer_id = 0;
    struct wlr_scene_node *node = nullptr;
};

class Core;
class Output;
class SparrowView;

/* enum sparrow_grab_type {
 *   SPARROW_GRAB_NONE = 0, SPARROW_GRAB_MOVE, SPARROW_GRAB_RESIZE,
 *  };
 *
 *  struct sparrow_grab_state {
 *   enum sparrow_grab_type type = {};
 *   uint32_t view_handle = 0;
 *   double start_cursor_x = 0;
 *   double start_cursor_y = 0;
 *   int start_view_x = 0;
 *   int start_view_y = 0;
 *   int start_view_width = 0;
 *   int start_view_height = 0;
 *   uint32_t resize_edges = 0;
 *  }; */

enum ScrollDirection
{
    ScrollDirectionNone = 0,
    ScrollDirectionUp,
    ScrollDirectionDown,
    ScrollDirectionLeft,
    ScrollDirectionRight,
};

enum ScrollEvent
{
    ScrollEventStop = 0,
    ScrollEventStart,
    ScrollEventUpdate,
};

// Cache for original wlroots scroll event parameters.
// Used to pass accurate scroll data through the Flutter-first path.
struct sparrow_scroll_cache
{
    double delta = 0;
    int32_t delta_discrete = 0;
    enum wl_pointer_axis_source source; // enum wl_pointer_axis_source
    enum wl_pointer_axis_relative_direction
    relative_direction; // enum wl_pointer_axis_relative_direction
    enum wl_pointer_axis orientation; // enum wl_pointer_axis
    uint32_t time_msec = 0;
    bool valid = false;
};

// Cache for original wlroots button event timestamp.
// Wayland clients expect timestamps from the hardware clock, not Flutter's
// clock.
struct sparrow_button_cache
{
    uint32_t time_msec = 0;
    bool valid = false;
    uint32_t button = 0;
};

enum sparrow_motion_cache_type
{
    SPARROW_MOTION_CACHE_NONE = 0,
    SPARROW_MOTION_CACHE_ABSOLUTE,
    SPARROW_MOTION_CACHE_RELATIVE,
};

struct sparrow_motion_cache
{
    uint32_t time_msec = 0;
    bool valid = false;
    sparrow_motion_cache_type type = SPARROW_MOTION_CACHE_NONE;
    uint32_t button = 0;
    double dx = 0;
    double dy = 0;
    double dx_unaccel = 0;
    double dy_unaccel = 0;
};

struct sparrow_input_state
{
    uint32_t mouse_button_mask    = 0;
    uint32_t fl_mouse_button_mask = 0;
    bool flutter_state_is_down    = false;
    // Accumulated state for cursor before frame.
    uint32_t acc_mouse_button_mask = 0;
    double acc_scroll_delta_x = 0;
    double acc_scroll_delta_y = 0;

    // Flutter cursor position (from platform channel events)
    // Used for grab operations when running nested
    double flutter_cursor_x = 0;
    double flutter_cursor_y = 0;

    bool pan_started = false;
    double pan_x     = 0.0;
    double pan_y     = 0.0;
    bool zoom_started   = false;
    bool rotate_started = false;
    double scale    = 1.0;
    double rotation = 0.0;
    bool is_compositor_swipe = false;

    // Cached scroll events for Flutter-first path (supports multi-axis
    // simultaneous events)
    std::vector<struct sparrow_scroll_cache> scrolls;

    // Cached button event timestamp for Flutter-first path
    struct sparrow_button_cache last_button = {};
    // struct sparrow_motion_cache last_motion = {};

    std::vector<struct sparrow_motion_cache> motions;

    // struct sparrow_grab_state grab = {};

    uint32_t caps = -1;
};

struct sparrow_input_device
{
    struct wl_list link;
    Core *instance = nullptr;
    struct wlr_input_device *device = nullptr;
    struct wl_listener destroy;
    void *data = nullptr;
};

static const uint8_t pointerDownEvent   = 1;
static const uint8_t pointerUpEvent     = 2;
static const uint8_t pointerHoverEvent  = 3;
static const uint8_t pointerMoveEvent   = 4;
static const uint8_t pointerEnterEvent  = 5;
static const uint8_t pointerExitEvent   = 6;
static const uint8_t pointerScrollEvent = 7;
[[maybe_unused]] static const uint8_t pointerUnknownEvent = 8;
static const uint8_t pointerPanZoomStartEvent  = 9;
static const uint8_t pointerPanZoomUpdateEvent = 10;
static const uint8_t pointerPanZoomEndEvent    = 11;
static const uint8_t pointerCancelEvent = 12;

void sparrow_seat_init();
void sparrow_seat_finish();

void set_surface_buttons(uint32_t surface_handle, int64_t buttons);
// Clear button state tracking for a surface (call when surface is destroyed)
void sparrow_clear_surface_buttons(uint32_t surface_handle);
int64_t get_surface_buttons(uint32_t surface_handle);
std::optional<PointerDeviceKind> to_device_kind(uint8_t n);

bool flutter_mouse_button_to_linux(int64_t flutter_button,
    uint32_t *linux_button_out);

void sparrow_seat_update_capabilities();
std::shared_ptr<touch_point> seat_touch_point_add(int32_t id);
std::shared_ptr<touch_point> seat_touch_point_get(int32_t id);
void seat_touch_point_delete(int32_t id);
uint64_t seat_get_flutter_timestamp(uint32_t wl_time_msec);
void seat_configure_libinput_device(struct wlr_input_device *device);
SparrowView *seat_get_focus();
int64_t seat_flutter_button_mask_from_linux(uint32_t button);

#endif
