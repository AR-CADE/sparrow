#include "secure_output.hpp"

#include <cstdlib>

#include "core.hpp"
#include "output.hpp"
#include "secure-output-unstable-v1-protocol.h"
#include "view.hpp"

struct SparrowSecurityResource
{
    struct wl_resource *resource = nullptr;
    struct wlr_surface *surface  = nullptr;
    bool only_visible_on_secure_output = false;
    struct wl_listener surface_commit;
    struct wl_listener surface_destroy;
    struct wl_list link;
};

static void update_surface_view_sensitivity(struct wlr_surface *surface,
    bool sensitive)
{
    if (!surface)
    {
        return;
    }

    const auto instance = Core::instance();
    if (!instance)
    {
        return;
    }

    struct wlr_surface *root =
        wlr_surface_get_root_surface(const_cast<struct wlr_surface*>(surface));
    SparrowView *view = instance->find_view_by_wlr_surface(root ? root : surface);
    if (!view)
    {
        return;
    }

    (void)sensitive;
    instance->recompute_view_sensitivity(view);
}

static void security_handle_surface_commit(struct wl_listener *listener,
    void *data)
{
    (void)data;
    SparrowSecurityResource *sec = wl_container_of(listener, sec, surface_commit);
    if (sec && sec->only_visible_on_secure_output && sec->surface)
    {
        update_surface_view_sensitivity(sec->surface, true);
    }
}

static void security_handle_surface_destroy(struct wl_listener *listener,
    void *data)
{
    (void)data;
    SparrowSecurityResource *sec =
        wl_container_of(listener, sec, surface_destroy);
    if (!sec)
    {
        return;
    }

    wl_list_remove(&sec->surface_commit.link);
    wl_list_init(&sec->surface_commit.link);
    wl_list_remove(&sec->surface_destroy.link);
    wl_list_init(&sec->surface_destroy.link);
    sec->surface = nullptr;
}

static void security_resource_destroy(struct wl_resource *resource)
{
    auto *sec = static_cast<SparrowSecurityResource*>(
        wl_resource_get_user_data(resource));
    if (!sec)
    {
        return;
    }

    struct wlr_surface *surf = sec->surface;
    wl_list_remove(&sec->link);
    if (sec->surface)
    {
        wl_list_remove(&sec->surface_commit.link);
        wl_list_remove(&sec->surface_destroy.link);
        sec->surface = nullptr;
    }

    delete sec;

    if (surf && !sparrow_surface_has_secure_output(surf))
    {
        update_surface_view_sensitivity(surf, false);
    }
}

static void security_handle_destroy(struct wl_client *client,
    struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void security_handle_only_visible_on_secure_output(struct wl_client *client,
    struct wl_resource *resource)
{
    (void)client;
    auto *sec = static_cast<SparrowSecurityResource*>(
        wl_resource_get_user_data(resource));
    if (!sec)
    {
        return;
    }

    sec->only_visible_on_secure_output = true;
    if (sec->surface)
    {
        update_surface_view_sensitivity(sec->surface, true);
    }
}

static const struct zcr_security_v1_interface security_impl = {
    .destroy = security_handle_destroy,
    .only_visible_on_secure_output =
        security_handle_only_visible_on_secure_output,
};

static void secure_output_handle_destroy(struct wl_client *client,
    struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void secure_output_handle_get_security(struct wl_client *client,
    struct wl_resource *resource, uint32_t id,
    struct wl_resource *surface_resource)
{
    auto *mgr = static_cast<struct sparrow_secure_output_manager_v1*>(
        wl_resource_get_user_data(resource));
    struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
    if (!mgr || !surface)
    {
        return;
    }

    SparrowSecurityResource *existing = nullptr;
    wl_list_for_each(existing, &mgr->resources, link)
    {
        if (existing->surface == surface)
        {
            wl_resource_post_error(resource,
                ZCR_SECURE_OUTPUT_V1_ERROR_SECURITY_EXISTS,
                "wl_surface already has a zcr_security_v1 object");
            return;
        }
    }

    struct wl_resource *sec_res =
        wl_resource_create(client, &zcr_security_v1_interface,
                           wl_resource_get_version(resource), id);
    if (!sec_res)
    {
        wl_client_post_no_memory(client);
        return;
    }

    auto *sec = new SparrowSecurityResource();
    sec->resource = sec_res;
    sec->surface  = surface;
    sec->only_visible_on_secure_output = false;

    sec->surface_commit.notify = security_handle_surface_commit;
    wl_signal_add(&surface->events.commit, &sec->surface_commit);

    sec->surface_destroy.notify = security_handle_surface_destroy;
    wl_signal_add(&surface->events.destroy, &sec->surface_destroy);

    wl_list_insert(&mgr->resources, &sec->link);

    wl_resource_set_implementation(sec_res, &security_impl, sec,
        security_resource_destroy);
}

static const struct zcr_secure_output_v1_interface secure_output_impl = {
    .destroy = secure_output_handle_destroy,
    .get_security = secure_output_handle_get_security,
};

static void secure_output_bind(struct wl_client *client, void *data,
    uint32_t version, uint32_t id)
{
    auto *mgr = static_cast<struct sparrow_secure_output_manager_v1*>(data);
    struct wl_resource *resource =
        wl_resource_create(client, &zcr_secure_output_v1_interface, version, id);
    if (!resource)
    {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &secure_output_impl, mgr, nullptr);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct sparrow_secure_output_manager_v1 *mgr =
        wl_container_of(listener, mgr, display_destroy);

    SparrowSecurityResource *sec = nullptr;
    SparrowSecurityResource *tmp = nullptr;
    wl_list_for_each_safe(sec, tmp, &mgr->resources, link)
    {
        wl_list_remove(&sec->link);
        if (sec->surface)
        {
            wl_list_remove(&sec->surface_commit.link);
            wl_list_remove(&sec->surface_destroy.link);
            sec->surface = nullptr;
        }

        if (sec->resource)
        {
            wl_resource_set_user_data(sec->resource, nullptr);
        }

        delete sec;
    }

    wl_list_remove(&mgr->display_destroy.link);
    if (mgr->global)
    {
        wl_global_destroy(mgr->global);
        mgr->global = nullptr;
    }

    delete mgr;
}

struct sparrow_secure_output_manager_v1 *sparrow_secure_output_init(struct wl_display *display)
{
    if (!display)
    {
        return nullptr;
    }

    auto *mgr = new sparrow_secure_output_manager_v1();
    wl_list_init(&mgr->resources);

    mgr->global = wl_global_create(display, &zcr_secure_output_v1_interface, 1,
        mgr, secure_output_bind);
    if (!mgr->global)
    {
        delete mgr;
        return nullptr;
    }

    mgr->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(display, &mgr->display_destroy);

    wlr_log(WLR_INFO,
        "Enabled secure-output-unstable-v1 protocol (zcr_secure_output_v1)");
    return mgr;
}

bool sparrow_surface_has_secure_output(const struct wlr_surface *surface)
{
    if (!surface)
    {
        return false;
    }

    const auto instance = Core::instance();
    if (!instance || !instance->secure_output_manager)
    {
        return false;
    }

    const struct wlr_surface *root =
        wlr_surface_get_root_surface(const_cast<struct wlr_surface*>(surface));

    SparrowSecurityResource *sec = nullptr;
    wl_list_for_each(sec, &instance->secure_output_manager->resources, link)
    {
        if (!sec->only_visible_on_secure_output || !sec->surface)
        {
            continue;
        }

        const struct wlr_surface *sec_root =
            wlr_surface_get_root_surface(sec->surface);
        if ((sec->surface == surface) || (sec_root == root))
        {
            return true;
        }
    }

    return false;
}
