#ifndef MESSAGES_H
#define MESSAGES_H

#include <cstdint>
#include <string>
#include "client_wrapper/encodable_value.h"

struct surface_pointer_event_message
{
    uint32_t surface_handle = 0;
    int64_t device  = 0;
    int64_t buttons = 0;
    int64_t embedder_id = 0;
    uint8_t device_kind = 0;
    uint8_t kind    = 0;
    uint8_t view_id = 0;
    double local_pos_x  = 0;
    double local_pos_y  = 0;
    double global_pos_x = 0;
    double global_pos_y = 0;
    int64_t platform_id = 0;
    int64_t pointer     = 0;
    double widget_size_x  = 0;
    double widget_size_y  = 0;
    double scroll_delta_x = 0;
    double scroll_delta_y = 0;
    double pan_x    = 0;
    double pan_y    = 0;
    double scale    = 1.0;
    double rotation = 0.0;
    uint8_t event_type = 0;
    int64_t timestamp  = 0;
};

bool decode_surface_pointer_event_message(const flutter::EncodableValue *value,
    struct surface_pointer_event_message *out);

struct surface_toplevel_set_size_message
{
    uint32_t surface_handle = 0;
    int64_t size_x = 0;
    int64_t size_y = 0;
};

bool decode_surface_toplevel_set_size_message(const flutter::EncodableValue *value,
    struct surface_toplevel_set_size_message *out);

struct surface_keyboard_key_message
{
    uint32_t surface_handle = 0;
    uint64_t keycode   = 0;
    uint8_t event_type = 0; // 0 - released, 1 - pressed
    int64_t timestamp  = 0;
};

bool decode_surface_keyboard_key_message(const flutter::EncodableValue *value,
    struct surface_keyboard_key_message *out);

struct surface_toplevel_set_maximized_message
{
    uint32_t surface_handle = 0;
    int64_t maximized = 0;
};

bool decode_surface_toplevel_set_maximized_message(const flutter::EncodableValue *value,
    struct surface_toplevel_set_maximized_message *out);

struct surface_toplevel_close_message
{
    uint32_t surface_handle = 0;
};

bool decode_surface_toplevel_close_message(const flutter::EncodableValue *value,
    struct surface_toplevel_close_message *out);

struct surface_begin_move_message
{
    uint32_t surface_handle = 0;
};

bool decode_surface_begin_move_message(const flutter::EncodableValue *value,
    struct surface_begin_move_message *out);

struct surface_begin_resize_message
{
    uint32_t surface_handle = 0;
    int64_t edges = 0;
};

bool decode_surface_begin_resize_message(const flutter::EncodableValue *value,
    struct surface_begin_resize_message *out);

struct surface_set_position_message
{
    uint32_t surface_handle = 0;
    int64_t x = 0;
    int64_t y = 0;
};

bool decode_surface_set_position_message(const flutter::EncodableValue *value,
    struct surface_set_position_message *out);

#endif
