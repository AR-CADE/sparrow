#include "popup_message.hpp"
#include <core.hpp>
#include <output.hpp>
#include <surface/popup.hpp>
#include <surface/view.hpp>

void send_popup_map(SparrowPopup *popup)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel || !popup || !popup->parent_view)
    {
        return;
    }

    // Get popup positioner geometry (relative to parent window geometry)
    struct wlr_box pos_geo = popup->xdg_popup->current.geometry;
    if ((pos_geo.width == 0) || (pos_geo.height == 0))
    {
        pos_geo = popup->xdg_popup->scheduled.geometry;
    }

    // Get popup's own window geometry (offset of visible content in buffer)
    struct wlr_box win_geo = popup->xdg_surface->current.geometry;
    if ((win_geo.width == 0) || (win_geo.height == 0))
    {
        win_geo = popup->xdg_surface->pending.geometry;
    }

    // Check for subsurfaces - Firefox/browsers render popup content to subsurfaces
    // The actual texture dimensions come from the content surface
    struct wlr_surface *surf = popup->xdg_surface->surface;
    struct wlr_surface *content_surface     = surf;
    struct wlr_subsurface *first_subsurface = nullptr;
    struct wlr_subsurface *subsurface = nullptr;
    wl_list_for_each(subsurface, &surf->current.subsurfaces_below, current.link)
    {
        if (subsurface && (subsurface->surface != nullptr) &&
            subsurface->surface->mapped)
        {
            first_subsurface = subsurface;
            break;
        }
    }
    if (first_subsurface == nullptr)
    {
        wl_list_for_each(subsurface, &surf->current.subsurfaces_above,
            current.link)
        {
            if (subsurface && (subsurface->surface != nullptr) &&
                subsurface->surface->mapped)
            {
                first_subsurface = subsurface;
                break;
            }
        }
    }

    if (first_subsurface != nullptr)
    {
        content_surface = first_subsurface->surface;
    }

    // Use the actual content surface dimensions to match the texture
    int width  = content_surface->current.width;
    int height = content_surface->current.height;

    // Fall back to geometry if content surface has no size yet
    if ((width == 0) || (height == 0))
    {
        width  = surf->current.width > 0 ? surf->current.width : pos_geo.width;
        height = surf->current.height > 0 ? surf->current.height : pos_geo.height;
    }

    // Calculate visible position (accumulating parent positioner offsets):
    popup->pos_x =
        pos_geo.x + (popup->parent_popup ? popup->parent_popup->pos_x : 0);
    popup->pos_y =
        pos_geo.y + (popup->parent_popup ? popup->parent_popup->pos_y : 0);

    // Buffer position for Flutter Texture (subtract popup's own shadow offset):
    popup->x     = popup->pos_x - win_geo.x;
    popup->y     = popup->pos_y - win_geo.y;
    popup->width = width;
    popup->height = height;

    wlr_log(
        WLR_INFO,
        "Popup map: handle=%d, parent=%d, is_child_popup=%i, pos=(%d,%d), "
        "size=%dx%d, surface=%dx%d "
        "(buf_scale=%d), content=%dx%d (buf_scale=%d), output=%s, scale=%.2f",
        popup->handle, popup->parent_view->handle, popup->parent_popup != nullptr,
        popup->x, popup->y, popup->width, popup->height, surf->current.width,
        surf->current.height, surf->current.scale, content_surface->current.width,
        content_surface->current.height, content_surface->current.scale,
        popup->current_output ? popup->current_output->wlr_output->name : "none",
        popup->output_scale);

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("handle"), flutter::EncodableValue((int64_t)popup->handle)},
        {flutter::EncodableValue("parent_handle"),
            flutter::EncodableValue((int64_t)popup->parent_view->handle)},
        {flutter::EncodableValue("x"), flutter::EncodableValue((int64_t)popup->x)},
        {flutter::EncodableValue("y"), flutter::EncodableValue((int64_t)popup->y)},
        {flutter::EncodableValue("width"), flutter::EncodableValue((int64_t)popup->width)},
        {flutter::EncodableValue("height"), flutter::EncodableValue((int64_t)popup->height)},
        {flutter::EncodableValue("texture_id"), flutter::EncodableValue((int64_t)popup->texture_id)},
        {flutter::EncodableValue("output_id"),
            flutter::EncodableValue((int64_t)(popup->current_output ? popup->current_output->id : 0))},
        {flutter::EncodableValue("output_scale"), flutter::EncodableValue(popup->output_scale)},
    };

    instance->wlroots_channel->InvokeMethod("popup_map",
        std::make_unique<flutter::EncodableValue>(map));
}

void send_popup_unmap(uint32_t handle)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel)
    {
        return;
    }

    wlr_log(WLR_INFO, "Popup unmap: handle=%d", handle);

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("handle"), flutter::EncodableValue((int64_t)handle)},
    };

    instance->wlroots_channel->InvokeMethod("popup_unmap",
        std::make_unique<flutter::EncodableValue>(map));
}
