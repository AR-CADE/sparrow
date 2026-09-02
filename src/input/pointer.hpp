#ifndef POINTER_H
#define POINTER_H

#include <sparrow/nonstd/wlroots-full.hpp>

class Core;

struct sparrow_pointer
{
    struct wl_list link;
    struct wlr_pointer *pointer     = nullptr;
    struct wlr_input_device *device = nullptr;
    struct wl_listener destroy;
};

void server_new_pointer(struct wlr_input_device *device);

void process_cursor_motion(uint32_t time, double dx, double dy,
    double dx_unaccel, double dy_unaccel,
    bool is_trackpad = false);

void sparrow_pointer_constraints_init(Core *core);
void sparrow_pointer_constraints_set_focus(Core *core, struct wlr_surface *surface);
void sparrow_pointer_constraints_deactivate(Core *core);
void handle_new_pointer_constraint(struct wl_listener *listener, void *data);

void on_server_cursor_motion(struct wl_listener *listener, void *data);
void on_server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void on_server_cursor_button(struct wl_listener *listener, void *data);
void on_server_cursor_axis(struct wl_listener *listener, void *data);
void on_server_cursor_frame(struct wl_listener *listener, void *data);
void on_seat_request_set_cursor_shape(struct wl_listener *listener, void *data);
void on_seat_request_cursor(struct wl_listener *listener, void *data);

void handle_swipe_begin(struct wl_listener *listener, void *data);
void handle_swipe_update(struct wl_listener *listener, void *data);
void handle_swipe_end(struct wl_listener *listener, void *data);
void handle_hold_end(struct wl_listener *listener, void *data);
void handle_hold_begin(struct wl_listener *listener, void *data);
void handle_pinch_end(struct wl_listener *listener, void *data);
void handle_pinch_update(struct wl_listener *listener, void *data);
void handle_pinch_begin(struct wl_listener *listener, void *data);

void handle_new_virtual_pointer(struct wl_listener *listener, void *data);

#endif
