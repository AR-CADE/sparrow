#ifndef SPARROW_SECURE_OUTPUT_HPP
#define SPARROW_SECURE_OUTPUT_HPP

#include <sparrow/nonstd/wlroots-full.hpp>

class Core;

struct sparrow_secure_output_manager_v1
{
    struct wl_global *global = nullptr;
    struct wl_list resources; // SparrowSecurityResource::link
    struct wl_listener display_destroy;
};

struct sparrow_secure_output_manager_v1 *sparrow_secure_output_init(
    struct wl_display *display);

bool sparrow_surface_has_secure_output(const struct wlr_surface *surface);

#endif // SPARROW_SECURE_OUTPUT_HPP
