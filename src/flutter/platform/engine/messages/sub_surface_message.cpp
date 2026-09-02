#include "sub_surface_message.hpp"
#include <core.hpp>
#include <surface/sub_surface.hpp>
#include <surface/view.hpp>

void sparrow_subsurface_send_position(SparrowSubSurface *sub)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel || !sub || !sub->parent_view)
    {
        return;
    }

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("handle"), flutter::EncodableValue((int64_t)sub->handle)},
        {flutter::EncodableValue("parent_handle"),
            flutter::EncodableValue((int64_t)sub->parent_view->handle)},
        {flutter::EncodableValue("x"), flutter::EncodableValue((int64_t)sub->x)},
        {flutter::EncodableValue("y"), flutter::EncodableValue((int64_t)sub->y)},
        {flutter::EncodableValue("width"), flutter::EncodableValue((int64_t)sub->width)},
        {flutter::EncodableValue("height"), flutter::EncodableValue((int64_t)sub->height)},
        {flutter::EncodableValue("buffer_width"), flutter::EncodableValue((int64_t)sub->buffer_width)},
        {flutter::EncodableValue("buffer_height"), flutter::EncodableValue((int64_t)sub->buffer_height)},
    };

    instance->wlroots_channel->InvokeMethod("subsurface_position",
        std::make_unique<flutter::EncodableValue>(map));
}

void send_subsurface_map(SparrowSubSurface *sub)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel || !sub || !sub->parent_view)
    {
        return;
    }

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("handle"), flutter::EncodableValue((int64_t)sub->handle)},
        {flutter::EncodableValue("parent_handle"),
            flutter::EncodableValue((int64_t)sub->parent_view->handle)},
        {flutter::EncodableValue("texture_id"), flutter::EncodableValue((int64_t)sub->texture_id)},
        {flutter::EncodableValue("x"), flutter::EncodableValue((int64_t)sub->x)},
        {flutter::EncodableValue("y"), flutter::EncodableValue((int64_t)sub->y)},
        {flutter::EncodableValue("width"), flutter::EncodableValue((int64_t)sub->width)},
        {flutter::EncodableValue("height"), flutter::EncodableValue((int64_t)sub->height)},
        {flutter::EncodableValue("buffer_width"), flutter::EncodableValue((int64_t)sub->buffer_width)},
        {flutter::EncodableValue("buffer_height"), flutter::EncodableValue((int64_t)sub->buffer_height)},
    };

    instance->wlroots_channel->InvokeMethod("subsurface_map",
        std::make_unique<flutter::EncodableValue>(map));
}

void send_subsurface_unmap(uint32_t handle, uint32_t parent_handle)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel)
    {
        return;
    }

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("handle"), flutter::EncodableValue((int64_t)handle)},
        {flutter::EncodableValue("parent_handle"), flutter::EncodableValue((int64_t)parent_handle)},
    };

    instance->wlroots_channel->InvokeMethod("subsurface_unmap",
        std::make_unique<flutter::EncodableValue>(map));
}
