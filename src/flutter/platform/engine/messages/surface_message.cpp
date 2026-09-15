#include "surface_message.hpp"
#include "flutter/platform/engine/callbacks/surface_callback.hpp"
#include <core.hpp>
#include <surface/view.hpp>

void send_surface_title(SparrowView *view)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api || !view || !view->toplevel)
    {
        return;
    }

    const char *title  = view->toplevel->title;
    const char *app_id = view->toplevel->app_id;

    instance->pigeon_flutter_api->SurfaceTitle(
        view->handle,
        title ? title : "",
        app_id ? app_id : "",
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "surface_title error: %s", err.message().c_str());
    });
}

void send_surface_map(SparrowView *view)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api || !view || !view->xdg_surface)
    {
        return;
    }

    int32_t pid;
    uint32_t uid, gid;
    wl_client_get_credentials(view->xdg_surface->client->client, &pid, &uid,
        &gid);

    // App uses CSD if it didn't negotiate SSD via xdg-decoration protocol
    const bool uses_csd = !view->uses_ssd;
    wlr_log(WLR_INFO,
        "Decoration mode: uses_ssd=%d, uses_csd=%d, geo_offset=(%d,%d)",
        view->uses_ssd, uses_csd, view->geo_x, view->geo_y);

    // Get actual buffer dimensions (includes shadow area for CSD apps)
    const int buffer_width  = view->xdg_surface->surface->current.width;
    const int buffer_height = view->xdg_surface->surface->current.height;

    int32_t min_w = 0, max_w = 0, min_h = 0, max_h = 0;
    if (view->toplevel != nullptr)
    {
        min_w = view->toplevel->current.min_width;
        max_w = view->toplevel->current.max_width;
        min_h = view->toplevel->current.min_height;
        max_h = view->toplevel->current.max_height;
    }

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("handle"), flutter::EncodableValue((int64_t)view->handle)},
        {flutter::EncodableValue("texture_id"), flutter::EncodableValue((int64_t)view->texture_id)},
        {flutter::EncodableValue("x"), flutter::EncodableValue((int64_t)view->x)},
        {flutter::EncodableValue("y"), flutter::EncodableValue((int64_t)view->y)},
        {flutter::EncodableValue("width"), flutter::EncodableValue((int64_t)view->width)},
        {flutter::EncodableValue("height"), flutter::EncodableValue((int64_t)view->height)},
        {flutter::EncodableValue("buffer_width"), flutter::EncodableValue((int64_t)buffer_width)},
        {flutter::EncodableValue("buffer_height"), flutter::EncodableValue((int64_t)buffer_height)},
        {flutter::EncodableValue("geo_x"), flutter::EncodableValue((int64_t)view->geo_x)},
        {flutter::EncodableValue("geo_y"), flutter::EncodableValue((int64_t)view->geo_y)},
        {flutter::EncodableValue("client_pid"), flutter::EncodableValue((int64_t)pid)},
        {flutter::EncodableValue("client_uid"), flutter::EncodableValue((int64_t)uid)},
        {flutter::EncodableValue("client_gid"), flutter::EncodableValue((int64_t)gid)},
        {flutter::EncodableValue("title"),
            flutter::EncodableValue((view->toplevel && view->toplevel->title) ? view->toplevel->title : "")},
        {flutter::EncodableValue("app_id"),
            flutter::EncodableValue((view->toplevel &&
                view->toplevel->app_id) ? view->toplevel->app_id : "")},
        {flutter::EncodableValue("maximized"), flutter::EncodableValue((int64_t)(view->maximized ? 1 : 0))},
        {flutter::EncodableValue("activated"), flutter::EncodableValue((int64_t)(view->activated ? 1 : 0))},
        {flutter::EncodableValue("uses_csd"), flutter::EncodableValue((int64_t)(uses_csd ? 1 : 0))},
        {flutter::EncodableValue("output_id"),
            flutter::EncodableValue((int64_t)(view->current_output ? view->current_output->id : 0))},
        {flutter::EncodableValue("output_scale"), flutter::EncodableValue(view->output_scale)},
        {flutter::EncodableValue("min_width"), flutter::EncodableValue((int64_t)min_w)},
        {flutter::EncodableValue("max_width"), flutter::EncodableValue((int64_t)max_w)},
        {flutter::EncodableValue("min_height"), flutter::EncodableValue((int64_t)min_h)},
        {flutter::EncodableValue("max_height"), flutter::EncodableValue((int64_t)max_h)},
    };

    instance->pigeon_flutter_api->SurfaceMap(
        map,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "surface_map error: %s", err.message().c_str());
    });

    // Initialize foreign toplevel handle (wlr_foreign_toplevel_management_v1)
    if ((instance->foreign_toplevel_manager != nullptr) &&
        (view->foreign_toplevel == nullptr))
    {
        view->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(
            instance->foreign_toplevel_manager);
        if (view->foreign_toplevel != nullptr)
        {
            view->foreign_toplevel->data = view;
            wlr_foreign_toplevel_handle_v1_set_title(
                view->foreign_toplevel,
                view->toplevel->title ? view->toplevel->title : "");
            wlr_foreign_toplevel_handle_v1_set_app_id(
                view->foreign_toplevel,
                view->toplevel->app_id ? view->toplevel->app_id : "");
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel,
                view->maximized);
            wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel,
                false);
            wlr_foreign_toplevel_handle_v1_set_activated(view->foreign_toplevel,
                view->activated);
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel,
                view->fullscreen);

            if (view->current_output && view->current_output->wlr_output)
            {
                wlr_foreign_toplevel_handle_v1_output_enter(
                    view->foreign_toplevel, view->current_output->wlr_output);
            }

            view->foreign_activate_request.notify = handle_foreign_activate_request;
            wl_signal_add(&view->foreign_toplevel->events.request_activate,
                &view->foreign_activate_request);

            view->foreign_close_request.notify = handle_foreign_close_request;
            wl_signal_add(&view->foreign_toplevel->events.request_close,
                &view->foreign_close_request);

            view->foreign_maximize_request.notify = handle_foreign_maximize_request;
            wl_signal_add(&view->foreign_toplevel->events.request_maximize,
                &view->foreign_maximize_request);

            view->foreign_minimize_request.notify = handle_foreign_minimize_request;
            wl_signal_add(&view->foreign_toplevel->events.request_minimize,
                &view->foreign_minimize_request);

            view->foreign_fullscreen_request.notify =
                handle_foreign_fullscreen_request;
            wl_signal_add(&view->foreign_toplevel->events.request_fullscreen,
                &view->foreign_fullscreen_request);
        }
    }

    // Initialize ext_foreign_toplevel handle (ext-foreign-toplevel-list-v1)
    if ((instance->ext_foreign_toplevel_list != nullptr) &&
        (view->ext_foreign_toplevel == nullptr))
    {
        struct wlr_ext_foreign_toplevel_handle_v1_state ext_state = {
            .title  = view->toplevel->title ? view->toplevel->title : "",
            .app_id = view->toplevel->app_id ? view->toplevel->app_id : "",
        };
        view->ext_foreign_toplevel = wlr_ext_foreign_toplevel_handle_v1_create(
            instance->ext_foreign_toplevel_list, &ext_state);
        if (view->ext_foreign_toplevel != nullptr)
        {
            view->ext_foreign_toplevel->data = view;
        }
    }
}

void send_surface_unmap(uint32_t handle)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api)
    {
        return;
    }

    instance->pigeon_flutter_api->SurfaceUnmap(
        handle,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "surface_unmap error: %s", err.message().c_str());
    });
}

void send_surface_geometry(SparrowView *view)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api || !view || !view->xdg_surface ||
        !view->xdg_surface->surface)
    {
        return;
    }

    struct wlr_surface *surface = view->xdg_surface->surface;

    instance->pigeon_flutter_api->SurfaceGeometry(
        view->handle,
        view->width,
        view->height,
        surface->current.width,
        surface->current.height,
        view->geo_x,
        view->geo_y,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "surface_geometry error: %s", err.message().c_str());
    });
}

void send_surface_minimize(SparrowView *view)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api || !view)
    {
        return;
    }

    instance->pigeon_flutter_api->SurfaceMinimize(
        view->handle,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "surface_minimize error: %s", err.message().c_str());
    });
}

void send_surface_request_activate(uint32_t handle,
    const char *token, const char *app_id)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api)
    {
        return;
    }

    instance->pigeon_flutter_api->SurfaceRequestActivate(
        handle,
        token ? token : "",
        app_id ? app_id : "",
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "surface_request_activate error: %s", err.message().c_str());
    });
}
