#ifndef LAYER_SURFACE_MESSAGE_HPP
#define LAYER_SURFACE_MESSAGE_HPP

#include <stdint.h>

struct SparrowLayerSurface;

void send_layer_surface_map(SparrowLayerSurface *layer_surface);
void send_layer_surface_unmap(uint32_t handle);

#endif
