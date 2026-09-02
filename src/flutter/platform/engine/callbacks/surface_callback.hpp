#ifndef SURFACE_CALLBACK_H
#define SURFACE_CALLBACK_H

#include "flutter_embedder.h"
#include <cstdint>
#include <sparrow/nonstd/wlroots-full.hpp>

class Core;
class SparrowView;

void handle_foreign_activate_request(struct wl_listener *listener, void *data);
void handle_foreign_close_request(struct wl_listener *listener, void *data);
void handle_foreign_maximize_request(struct wl_listener *listener, void *data);
void handle_foreign_minimize_request(struct wl_listener *listener, void *data);
void handle_foreign_fullscreen_request(struct wl_listener *listener, void *data);

void sparrow_new_xdg_toplevel(struct wl_listener *listener, void *data);

void sparrow_handle_surface_toplevel_set_size(uint32_t surface_handle, int width, int height);
void sparrow_handle_surface_toplevel_set_maximized(uint32_t surface_handle, bool maximized);
bool sparrow_handle_surface_toplevel_close(uint32_t surface_handle);
void sparrow_handle_surface_focus(uint32_t surface_handle);
void sparrow_handle_surface_set_position(uint32_t surface_handle, int x, int y);

void sparrow_handle_surface_request_resize(uint32_t surface_handle, int width, int height,
    uint64_t request_id);
void sparrow_handle_surface_end_resize(uint32_t surface_handle);

#endif
