#include "layer_surface_message.hpp"
#include <core.hpp>
#include <output.hpp>
#include <surface/layer_surface.hpp>

void send_layer_surface_map(SparrowLayerSurface *layer_surface)
{
    const auto instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api || !layer_surface ||
        !layer_surface->layer_surface || !layer_surface->layer_surface->surface)
    {
        return;
    }

    struct wlr_layer_surface_v1 *wlr_ls = layer_surface->layer_surface;
    struct wlr_surface *surf = wlr_ls->surface;

    const int buf_w = surf->current.width;
    const int buf_h = surf->current.height;
    const int eff_w = (layer_surface->width > 0) ? layer_surface->width : buf_w;
    const int eff_h = (layer_surface->height > 0) ? layer_surface->height : buf_h;

    wlr_log(
        WLR_INFO,
        "LayerSurface map: handle=%d, layer=%d, anchor=%d, pos=(%d,%d), size=%dx%d, buf=%dx%d, tex=%ld, scope='%s'",
        layer_surface->handle, (int)wlr_ls->current.layer, wlr_ls->current.anchor,
        layer_surface->x, layer_surface->y, eff_w, eff_h, buf_w, buf_h,
        (long)layer_surface->texture_id,
        wlr_ls->namespace_t ? wlr_ls->namespace_t : "");

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("handle"),
            flutter::EncodableValue((int64_t)layer_surface->handle)},
        {flutter::EncodableValue("texture_id"),
            flutter::EncodableValue((int64_t)layer_surface->texture_id)},
        {flutter::EncodableValue("layer"),
            flutter::EncodableValue((int64_t)wlr_ls->current.layer)},
        {flutter::EncodableValue("anchor"),
            flutter::EncodableValue((int64_t)wlr_ls->current.anchor)},
        {flutter::EncodableValue("x"),
            flutter::EncodableValue((int64_t)layer_surface->x)},
        {flutter::EncodableValue("y"),
            flutter::EncodableValue((int64_t)layer_surface->y)},
        {flutter::EncodableValue("width"),
            flutter::EncodableValue((int64_t)eff_w)},
        {flutter::EncodableValue("height"),
            flutter::EncodableValue((int64_t)eff_h)},
        {flutter::EncodableValue("buffer_width"),
            flutter::EncodableValue((int64_t)buf_w)},
        {flutter::EncodableValue("buffer_height"),
            flutter::EncodableValue((int64_t)buf_h)},
        {flutter::EncodableValue("margin_top"),
            flutter::EncodableValue((int64_t)wlr_ls->current.margin.top)},
        {flutter::EncodableValue("margin_bottom"),
            flutter::EncodableValue((int64_t)wlr_ls->current.margin.bottom)},
        {flutter::EncodableValue("margin_left"),
            flutter::EncodableValue((int64_t)wlr_ls->current.margin.left)},
        {flutter::EncodableValue("margin_right"),
            flutter::EncodableValue((int64_t)wlr_ls->current.margin.right)},
        {flutter::EncodableValue("exclusive_zone"),
            flutter::EncodableValue((int64_t)wlr_ls->current.exclusive_zone)},
        {flutter::EncodableValue("keyboard_interactive"),
            flutter::EncodableValue(
                wlr_ls->current.keyboard_interactive !=
                ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)},
        {flutter::EncodableValue("output_id"),
            flutter::EncodableValue(
                (int64_t)(layer_surface->current_output ?
                    layer_surface->current_output->id :
                    0))},
        {flutter::EncodableValue("scope"),
            flutter::EncodableValue(
                wlr_ls->namespace_t ? std::string(wlr_ls->namespace_t) : "")},
    };

    instance->pigeon_flutter_api->LayerSurfaceMap(
        map, [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "layer_surface_map error: %s", err.message().c_str());
    });
}

void send_layer_surface_unmap(uint32_t handle)
{
    const auto instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api)
    {
        return;
    }

    wlr_log(WLR_INFO, "LayerSurface unmap: handle=%d", handle);

    instance->pigeon_flutter_api->LayerSurfaceUnmap(
        handle, [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "layer_surface_unmap error: %s", err.message().c_str());
    });
}
