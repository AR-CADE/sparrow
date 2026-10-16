#include "messages.hpp"

static inline int64_t enc_get_int(const flutter::EncodableValue& val, int64_t default_val = 0)
{
    if (std::holds_alternative<int32_t>(val))
    {
        return std::get<int32_t>(val);
    }

    if (std::holds_alternative<int64_t>(val))
    {
        return std::get<int64_t>(val);
    }

    if (std::holds_alternative<bool>(val))
    {
        return std::get<bool>(val) ? 1 : 0;
    }

    return default_val;
}

static inline double enc_get_double(const flutter::EncodableValue& val, double default_val = 0.0)
{
    if (std::holds_alternative<double>(val))
    {
        return std::get<double>(val);
    }

    if (std::holds_alternative<int32_t>(val))
    {
        return (double)std::get<int32_t>(val);
    }

    if (std::holds_alternative<int64_t>(val))
    {
        return (double)std::get<int64_t>(val);
    }

    return default_val;
}

bool decode_surface_pointer_event_message(const flutter::EncodableValue *value,
    struct surface_pointer_event_message *out)
{
    if (!value)
    {
        return false;
    }

    const auto *list = std::get_if<flutter::EncodableList>(value);
    if (!list)
    {
        return false;
    }

    if ((list->size() != 33) && (list->size() != 37))
    {
        return false;
    }

    const auto& v = *list;
    out->surface_handle = (uint32_t)enc_get_int(v[0]);
    out->buttons = enc_get_int(v[1]);
    out->device  = enc_get_int(v[4]);
    out->embedder_id = enc_get_int(v[7]);
    out->device_kind = (uint8_t)enc_get_int(v[8]);
    out->local_pos_x = enc_get_double(v[11]);
    out->local_pos_y = enc_get_double(v[12]);
    out->platform_id = enc_get_int(v[15]);
    out->pointer     = enc_get_int(v[16]);
    out->global_pos_x  = enc_get_double(v[17]);
    out->global_pos_y  = enc_get_double(v[18]);
    out->timestamp     = enc_get_int(v[25]);
    out->event_type    = (uint8_t)enc_get_int(v[26]);
    out->widget_size_x = enc_get_double(v[27]);
    out->widget_size_y = enc_get_double(v[28]);
    out->scroll_delta_x = enc_get_double(v[29]);
    out->scroll_delta_y = enc_get_double(v[30]);
    out->kind    = (uint8_t)enc_get_int(v[31]);
    out->view_id = (uint8_t)enc_get_int(v[32]);

    if (list->size() >= 37)
    {
        out->pan_x    = enc_get_double(v[33]);
        out->pan_y    = enc_get_double(v[34]);
        out->scale    = enc_get_double(v[35], 1.0);
        out->rotation = enc_get_double(v[36]);
    }

    return true;
}

bool decode_surface_keyboard_key_message(const flutter::EncodableValue *value,
    struct surface_keyboard_key_message *out)
{
    if (!value)
    {
        return false;
    }

    const auto *list = std::get_if<flutter::EncodableList>(value);
    if (!list || (list->size() != 4))
    {
        return false;
    }

    const auto& v = *list;
    out->surface_handle = (uint32_t)enc_get_int(v[0]);
    out->keycode    = (uint64_t)enc_get_int(v[1]);
    out->event_type = (uint8_t)enc_get_int(v[2]);
    out->timestamp  = enc_get_int(v[3]);

    return true;
}

bool decode_surface_toplevel_set_size_message(const flutter::EncodableValue *value,
    struct surface_toplevel_set_size_message *out)
{
    if (!value)
    {
        return false;
    }

    const auto *list = std::get_if<flutter::EncodableList>(value);
    if (!list || (list->size() != 3))
    {
        return false;
    }

    const auto& v = *list;
    out->surface_handle = (uint32_t)enc_get_int(v[0]);
    out->size_x = enc_get_int(v[1]);
    out->size_y = enc_get_int(v[2]);

    return true;
}

bool decode_surface_toplevel_set_maximized_message(const flutter::EncodableValue *value,
    struct surface_toplevel_set_maximized_message *out)
{
    if (!value)
    {
        return false;
    }

    const auto *list = std::get_if<flutter::EncodableList>(value);
    if (!list || (list->size() != 2))
    {
        return false;
    }

    const auto& v = *list;
    out->surface_handle = (uint32_t)enc_get_int(v[0]);
    out->maximized = enc_get_int(v[1]);

    return true;
}

bool decode_surface_toplevel_close_message(const flutter::EncodableValue *value,
    struct surface_toplevel_close_message *out)
{
    if (!value)
    {
        return false;
    }

    const auto *list = std::get_if<flutter::EncodableList>(value);
    if (!list || (list->size() != 1))
    {
        return false;
    }

    out->surface_handle = (uint32_t)enc_get_int((*list)[0]);
    return true;
}

bool decode_surface_begin_move_message(const flutter::EncodableValue *value,
    struct surface_begin_move_message *out)
{
    if (!value)
    {
        return false;
    }

    const auto *list = std::get_if<flutter::EncodableList>(value);
    if (!list || (list->size() != 1))
    {
        return false;
    }

    out->surface_handle = (uint32_t)enc_get_int((*list)[0]);
    return true;
}

bool decode_surface_begin_resize_message(const flutter::EncodableValue *value,
    struct surface_begin_resize_message *out)
{
    if (!value)
    {
        return false;
    }

    const auto *list = std::get_if<flutter::EncodableList>(value);
    if (!list || (list->size() != 2))
    {
        return false;
    }

    const auto& v = *list;
    out->surface_handle = (uint32_t)enc_get_int(v[0]);
    out->edges = enc_get_int(v[1]);

    return true;
}

bool decode_surface_set_position_message(const flutter::EncodableValue *value,
    struct surface_set_position_message *out)
{
    if (!value)
    {
        return false;
    }

    const auto *list = std::get_if<flutter::EncodableList>(value);
    if (!list || (list->size() != 3))
    {
        return false;
    }

    const auto& v = *list;
    out->surface_handle = (uint32_t)enc_get_int(v[0]);
    out->x = enc_get_int(v[1]);
    out->y = enc_get_int(v[2]);

    return true;
}
