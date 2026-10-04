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
static bool got_resumed = false;
static bool xdg_configured = false;

static void handle_idled(void *data, struct ext_idle_notification_v1 *notif)
{
    (void)data;
    (void)notif;
    printf("[TEST] IDLED event received!\n");
    got_idled = true;
}

static void handle_resumed(void *data, struct ext_idle_notification_v1 *notif)
{
    (void)data;
    (void)notif;
    printf("[TEST] RESUMED event received!\n");
    got_resumed = true;
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
    xdg_configured = true;
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
    int fd = memfd_create("test-redraw-idle-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
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
        fprintf(stderr, "Missing required globals: comp=%p shm=%p seat=%p idle=%p wm=%p\n",
            (void*)compositor, (void*)shm, (void*)seat, (void*)idle_notifier, (void*)wm_base);
        return 1;
    }

    // Register idle notification with 1000ms timeout
    printf("[TEST] Registering idle notification (timeout = 1000ms)...\n");
    struct ext_idle_notification_v1 *notif =
        ext_idle_notifier_v1_get_idle_notification(idle_notifier, 1000, seat);
    ext_idle_notification_v1_add_listener(notif, &notif_listener, NULL);

    // Create xdg toplevel window
    const int width = 400;
    const int height = 300;
    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(toplevel, "RPCS3 Video Preview Simulator");
    xdg_toplevel_set_app_id(toplevel, "rpcs3");

    struct wl_buffer *buffer = create_buffer(width, height);
    if (!buffer)
    {
        fprintf(stderr, "Failed to create SHM buffer\n");
        return 1;
    }

    // Initial commit to get configure
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    // Attach buffer and commit
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, width, height);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    printf("[TEST] Window mapped. Starting Phase 1: continuous redraws (30 FPS) for 2.5 seconds...\n");
    printf("[TEST] (Since idle timeout is 1000ms, staying awake for 2500ms proves redraws prevent idle)\n");

    const int frames = 75; // 75 frames * 33ms ≈ 2500ms
    for (int i = 0; i < frames; i++)
    {
        wl_surface_attach(surface, buffer, 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, width, height);
        wl_surface_commit(surface);

        wl_display_flush(display);
        usleep(33333); // ~33ms
        wl_display_dispatch_pending(display);

        if (got_idled)
        {
            fprintf(stderr, "[TEST] FAILED: IDLED event received at frame %d (~%d ms)! Redraws failed to prevent idle!\n",
                i, i * 33);
            ext_idle_notification_v1_destroy(notif);
            wl_display_disconnect(display);
            return 2;
        }
    }

    printf("[TEST] Phase 1 PASSED: Kept awake for 2.5s via redraws alone!\n");
    printf("[TEST] Starting Phase 2: Stopping redraws, waiting for idle event (up to 2.5s)...\n");

    for (int i = 0; i < 25; i++)
    {
        wl_display_dispatch_pending(display);
        if (got_idled)
        {
            break;
        }
        usleep(100000); // 100ms
        wl_display_dispatch(display);
    }

    if (!got_idled)
    {
        fprintf(stderr, "[TEST] FAILED: IDLED event was NOT received after redraws stopped!\n");
        ext_idle_notification_v1_destroy(notif);
        wl_display_disconnect(display);
        return 3;
    }

    printf("[TEST] Phase 2 PASSED: IDLED event received as expected after redraws ceased!\n");
    printf("[TEST] ALL TESTS PASSED SUCCESSFULLY!\n");

    ext_idle_notification_v1_destroy(notif);
    wl_surface_destroy(surface);
    wl_display_disconnect(display);
    return 0;
}
