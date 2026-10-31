#ifndef SPARROW_SCREENCOPY_HPP
#define SPARROW_SCREENCOPY_HPP

#include <wayland-server-core.h>

struct sparrow_screencopy_manager_v1;
struct wlr_render_pass;
struct wlr_output;
struct wlr_box;

struct sparrow_screencopy_manager_v1 *sparrow_screencopy_manager_v1_create(
    struct wl_display *display);

void sparrow_censor_output_render_pass(struct wlr_output *output,
    const struct wlr_box *capture_box, struct wlr_render_pass *pass);

#endif // SPARROW_SCREENCOPY_HPP
