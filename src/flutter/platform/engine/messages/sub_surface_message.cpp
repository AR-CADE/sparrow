#include "sub_surface_message.hpp"
#include <core.hpp>
#include <surface/sub_surface.hpp>
#include <surface/view.hpp>

void sparrow_subsurface_send_position(SparrowSubSurface *sub)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api || !sub || !sub->parent_view)
    {
        return;
    }

    instance->pigeon_flutter_api->SubsurfacePosition(
        sub->handle,
        sub->parent_view->handle,
        sub->x,
        sub->y,
        sub->width,
        sub->height,
        sub->buffer_width,
        sub->buffer_height,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "subsurface_position error: %s", err.message().c_str());
    });
}

void send_subsurface_map(SparrowSubSurface *sub)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api || !sub || !sub->parent_view)
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

    instance->pigeon_flutter_api->SubsurfaceMap(
        map,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "subsurface_map error: %s", err.message().c_str());
    });
}

void send_subsurface_unmap(uint32_t handle, uint32_t parent_handle)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api)
    {
        return;
    }

    instance->pigeon_flutter_api->SubsurfaceUnmap(
        handle,
        parent_handle,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "subsurface_unmap error: %s", err.message().c_str());
    });
}
