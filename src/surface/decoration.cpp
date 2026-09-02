#include <sparrow/nonstd/wlroots-full.hpp>

#include "decoration.hpp"
#include "core.hpp"
#include "surface.hpp"
#include "view.hpp"
#include "flutter/platform/engine/messages/decoration_message.hpp"

// Handler for decoration mode request - client wants to change decoration mode
static void handle_decoration_request_mode(struct wl_listener *listener, void *data)
{
    SparrowView *view = wl_container_of(listener, view, decoration_request_mode);

    wlr_log(WLR_INFO,
        "=== DECORATION MODE REQUEST from client (requested=%d) ===",
        view->decoration->requested_mode);

    if (view->toplevel->base->initialized)
    {
        // Always enforce server-side decorations regardless of what client wants
        wlr_xdg_toplevel_decoration_v1_set_mode(
            view->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    wlr_log(WLR_INFO, "Enforced SSD mode for view %d", view->handle);
}

// Handler for decoration destroy - clean up when decoration is destroyed
static void handle_decoration_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    SparrowView *view = wl_container_of(listener, view, decoration_destroy);
    if (!view)
    {
        return;
    }

    wlr_log(WLR_INFO, "Decoration destroyed for view %d", view->handle);

    if (view->decoration_request_mode.link.next && view->decoration_request_mode.link.prev)
    {
        wl_list_remove(&view->decoration_request_mode.link);
        wl_list_init(&view->decoration_request_mode.link);
    }

    if (view->decoration_destroy.link.next && view->decoration_destroy.link.prev)
    {
        wl_list_remove(&view->decoration_destroy.link);
        wl_list_init(&view->decoration_destroy.link);
    }

    view->decoration = nullptr;
}

// Handler for new xdg-decoration requests - tell apps to use server-side
// decorations
void sparrow_handle_new_toplevel_decoration(struct wl_listener *listener, void *data)
{
    Core *instance = Core::instance();

    struct wlr_xdg_toplevel_decoration_v1 *decoration =
        static_cast<wlr_xdg_toplevel_decoration_v1*>(data);

    if (!decoration)
    {
        return;
    }

    wlr_log(WLR_INFO, "=== XDG DECORATION REQUEST from client ===");

    // Find the view that owns this toplevel
    SparrowView *view = instance->find_view_by_toplevel(decoration->toplevel);

    if (view != nullptr)
    {
        view->decoration = decoration;

        // Listen for mode change requests and destroy
        view->decoration_request_mode.notify = handle_decoration_request_mode;
        wl_signal_add(&decoration->events.request_mode,
            &view->decoration_request_mode);

        view->decoration_destroy.notify = handle_decoration_destroy;
        wl_signal_add(&decoration->events.destroy, &view->decoration_destroy);

        if (view->toplevel && view->toplevel->base &&
            view->toplevel->base->initialized)
        {
            wlr_xdg_toplevel_decoration_v1_set_mode(
                decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }

        wlr_log(WLR_INFO,
            "Linked XDG decoration to view %d",
            view->handle);

        // Notify Flutter of decoration change (fixes timing issue where surface_map
        // might be sent before decoration negotiation completes)
        sparrow_send_decoration_update(view);
    }

    wlr_log(WLR_INFO, "Set SSD mode for surface");
}

// Handler for legacy KDE server decoration protocol (used by GTK3, Firefox,
// etc.)
void sparrow_handle_new_server_decoration(struct wl_listener *listener, void *data)
{
    // sparrow_instance *instance =
    // wl_container_of(listener, instance, new_server_decoration);
    Core *instance = Core::instance();

    const struct wlr_server_decoration *decoration =
        static_cast<wlr_server_decoration*>(data);

    if (!decoration)
    {
        return;
    }

    wlr_log(WLR_INFO,
        "=== LEGACY KDE DECORATION from client (surface=%p, mode=%d) ===",
        (void*)decoration->surface, decoration->mode);

    // The default mode is already SERVER, but let's find and mark the view
    // The decoration->surface is the wlr_surface, we need to find the view that
    // owns it
    SparrowView *view = instance->find_view_by_wlr_surface(decoration->surface);
    if (view && view->toplevel && view->toplevel->base && view->toplevel->base->initialized)
    {
        view->uses_ssd = (decoration->mode == WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

        wlr_log(
            WLR_INFO,
            "Linked legacy decoration to view %d, mode=%d (SERVER=2)",
            view->handle, decoration->mode);

        // Notify Flutter of decoration change
        sparrow_send_decoration_update(view);
    }
}
