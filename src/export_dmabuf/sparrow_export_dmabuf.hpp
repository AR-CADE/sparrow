#ifndef SPARROW_EXPORT_DMABUF_HPP
#define SPARROW_EXPORT_DMABUF_HPP

#include <wayland-server-core.h>

struct sparrow_export_dmabuf_manager_v1;

struct sparrow_export_dmabuf_manager_v1 *sparrow_export_dmabuf_manager_v1_create(
    struct wl_display *display);

#endif // SPARROW_EXPORT_DMABUF_HPP
