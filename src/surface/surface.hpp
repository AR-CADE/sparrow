#ifndef SURFACE_H
#define SURFACE_H

#include <flutter_embedder.h>
#include <sparrow/nonstd/wlroots-full.hpp>

class SparrowView;
class Output;
class Core;
class SparrowPopup;

void sparrow_new_xdg_toplevel(struct wl_listener *listener, void *data);
void sparrow_handle_xdg_activation_request_activate(struct wl_listener *listener, void *data);

#endif
