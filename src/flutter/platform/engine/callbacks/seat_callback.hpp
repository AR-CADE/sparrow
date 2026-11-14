#ifndef SEAT_CALLBACK_H
#define SEAT_CALLBACK_H

#include <cstdint>

namespace sparrow
{
class SurfacePointerEventMessage;
class SurfaceKeyboardKeyMessage;
}

class Core;

void sparrow_handle_surface_pointer_event(const sparrow::SurfacePointerEventMessage& message);
void sparrow_handle_popup_pointer_event(const sparrow::SurfacePointerEventMessage& message);
void sparrow_handle_layer_surface_pointer_event(const sparrow::SurfacePointerEventMessage& message);
void sparrow_handle_surface_keyboard_key(const sparrow::SurfaceKeyboardKeyMessage& message);
void sparrow_handle_surface_begin_move(uint32_t surface_handle);
void sparrow_handle_surface_begin_resize(uint32_t surface_handle, int64_t edges);

#endif
