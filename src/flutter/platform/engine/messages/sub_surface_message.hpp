#ifndef SUB_SURFACE_MESSAGE_H
#define SUB_SURFACE_MESSAGE_H

#include <cstdint>
class Core;
class SparrowSubSurface;
void sparrow_subsurface_send_position(SparrowSubSurface *sub);
void send_subsurface_map(SparrowSubSurface *sub);
void send_subsurface_unmap(uint32_t handle,
    uint32_t parent_handle);

#endif
