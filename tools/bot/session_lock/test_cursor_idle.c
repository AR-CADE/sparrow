#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <wayland-client.h>

#include "ext-idle-notify-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;
static struct wl_seat *seat = NULL;
static struct ext_idle_notifier_v1 *idle_notifier = NULL;
static struct xdg_wm_base *wm_base = NULL;

static bool got_idled = false;

static void handle_idled(void *data, struct ext_idle_notification_v1 *notif)
{
    (void)data;
    (void)notif;
    printf("[TEST-CURSOR] IDLED event received as expected!\n");
    got_idled = true;
}

static void handle_resumed(void *data, struct ext_idle_notification_v1 *notif)
{
    (void)data;
    (void)notif;
}

static const struct ext_idle_notification_v1_listener notif_listener = {
    .idled = handle_idled,
    .resumed = handle_resumed,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
    int32_t width, int32_t height, struct wl_array *states)
{
    (void)data; (void)toplevel; (void)width; (void)height; (void)states;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)data; (void)toplevel;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void registry_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    (void)data; (void)version;
    if (strcmp(interface, "wl_compositor") == 0)
    {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    }
    else if (strcmp(interface, "wl_shm") == 0)
    {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, "wl_seat") == 0)
    {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    }
    else if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0)
    {
        idle_notifier = wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1);
    }
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int create_shm_file(off_t size) {
    int fd = memfd_create("test-cursor-idle-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static struct wl_buffer *create_buffer(int width, int height) {
    int stride = width * 4;
    int size = stride * height;
    int fd = create_shm_file(size);
    if (fd < 0) return NULL;

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
    {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !shm || !seat || !idle_notifier || !wm_base)
    {
        fprintf(stderr, "Missing required globals\n");
        return 1;
    }

    // Register idle notification with 1000ms timeout
    struct ext_idle_notification_v1 *notif =
        ext_idle_notifier_v1_get_idle_notification(idle_notifier, 1000, seat);
    ext_idle_notification_v1_add_listener(notif, &notif_listener, NULL);

    const int width = 400;
    const int height = 300;
    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(toplevel, "Terminal Blinking Cursor Simulator");
    xdg_toplevel_set_app_id(toplevel, "terminal");

    struct wl_buffer *buffer = create_buffer(width, height);
    if (!buffer)
    {
        return 1;
    }

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, width, height);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    printf("[TEST-CURSOR] Simulating 1Hz blinking cursor (10x20 = 200 px² damage) for 2.0s...\n");
    // Blink every 500ms for 2.0s
    for (int i = 0; i < 20; i++)
    {
        // Only 10x20 cursor damage rect
        wl_surface_attach(surface, buffer, 0, 0);
        wl_surface_damage_buffer(surface, 50, 50, 10, 20);
        wl_surface_commit(surface);

        wl_display_flush(display);
        usleep(100000); // 100ms
        wl_display_dispatch(display);

        if (got_idled)
        {
            printf("[TEST-CURSOR] SUCCESS: IDLED event received at iteration %d despite blinking cursor!\n", i);
            ext_idle_notification_v1_destroy(notif);
            wl_surface_destroy(surface);
            wl_display_disconnect(display);
            return 0;
        }
    }

    if (!got_idled)
    {
        fprintf(stderr, "[TEST-CURSOR] FAILED: Blinking cursor incorrectly prevented idle!\n");
        ext_idle_notification_v1_destroy(notif);
        wl_surface_destroy(surface);
        wl_display_disconnect(display);
        return 2;
    }

    return 0;
}
