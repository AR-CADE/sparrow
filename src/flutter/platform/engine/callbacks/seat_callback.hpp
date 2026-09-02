#ifndef SEAT_CALLBACK_H
#define SEAT_CALLBACK_H

#include "flutter/platform/messages.hpp"
#include <cstdint>

class Core;

void sparrow_handle_surface_pointer_event(const surface_pointer_event_message& message);
void sparrow_handle_popup_pointer_event(const surface_pointer_event_message& message);
void sparrow_handle_surface_keyboard_key(const surface_keyboard_key_message& message);
void sparrow_handle_surface_begin_move(uint32_t surface_handle);
void sparrow_handle_surface_begin_resize(uint32_t surface_handle, int64_t edges);

#endif
