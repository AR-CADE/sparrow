#ifndef SURFACE_MESSAGE_H
#define SURFACE_MESSAGE_H

#include <cstdint>
class Core;
class SparrowView;

void send_surface_map(SparrowView *view);
void send_surface_unmap(uint32_t handle);
void send_surface_title(SparrowView *view);
void sparrow_send_decoration_update(SparrowView *view);
void send_surface_geometry(SparrowView *view);
void send_surface_request_activate(uint32_t handle,
    const char *token, const char *app_id);
void send_surface_minimize(SparrowView *view);

#endif
