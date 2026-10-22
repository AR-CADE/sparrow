#ifndef DECORATION_H
#define DECORATION_H

#include <sparrow/nonstd/wlroots-full.hpp>

// Handler for new xdg-decoration requests - tell apps to use server-side decorations
void sparrow_handle_new_toplevel_decoration(struct wl_listener *listener, void *data);

// Handler for legacy KDE server decoration protocol (used by GTK3, Firefox, etc.)
void sparrow_handle_new_server_decoration(struct wl_listener *listener, void *data);

#endif
