#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <sparrow/nonstd/wlroots-full.hpp>

class Core;

struct sparrow_keyboard
{
    struct wl_list link;
    struct wlr_keyboard *keyboard = nullptr;
    struct xkb_compose_state *compose_state = nullptr;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

void keyboard_handle_modifiers(struct wl_listener *listener, void *data);
void server_new_keyboard(struct wlr_input_device *device);
void keyboard_destroy(struct wl_listener *listener, void *data);
void keyboard_handle_key(struct wl_listener *listener, void *data);
void handle_new_virtual_keyboard(struct wl_listener *listener, void *data);

#endif
