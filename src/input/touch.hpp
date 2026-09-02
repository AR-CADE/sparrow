#ifndef TOUCH_H
#define TOUCH_H

#include <sparrow/nonstd/wlroots-full.hpp>

class Core;
class Output;

enum TouchEvent
{
    TouchEventkDown        = 1 << 0,
    TouchEventkUp          = 1 << 1,
    TouchEventkMotion      = 1 << 2,
    TouchEventkCancel      = 1 << 3,
    TouchEventkShape       = 1 << 4,
    TouchEventkOrientation = 1 << 5,
};

struct touch_point
{
    explicit touch_point(int32_t point_id)
    {
        id = point_id;
    }

    bool valid = false;
    int32_t id = 0;
    uint32_t event_mask = 0;
    double sx = 0, sy = 0;
    // double x, y;
    uint32_t time_msec = 0;
    struct wlr_scene_node *node = nullptr;
};

struct sparrow_touch
{
    struct wl_list link;
    Core *instance = nullptr;
    struct wlr_touch *wlr_touch     = nullptr;
    struct wlr_input_device *device = nullptr;
    struct wl_listener destroy;
};

void on_server_cursor_touch_down(struct wl_listener *listener, void *data);
void on_server_cursor_touch_up(struct wl_listener *listener, void *data);
void on_server_cursor_touch_motion(struct wl_listener *listener, void *data);
void on_server_cursor_touch_cancel(struct wl_listener *listener, void *data);
void on_server_cursor_touch_frame(struct wl_listener *listener, void *data);
void map_touch_to_output(struct wlr_input_device *&device, Output *output);
void map_touch_to_output(struct wlr_input_device *&device);
void server_new_touch(struct wlr_input_device *device);

#endif
