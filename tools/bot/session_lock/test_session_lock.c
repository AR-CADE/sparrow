#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <wayland-client.h>

#include "ext-session-lock-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"

static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;
static struct wl_output *output = NULL;
static struct ext_session_lock_manager_v1 *lock_manager = NULL;
static struct zwp_idle_inhibit_manager_v1 *idle_inhibit_manager = NULL;
static int has_idle_notify = 0;

static int locked_received = 0;
static int finished_received = 0;
static int configured_received = 0;

static int create_shm_file(off_t size) {
    int fd = memfd_create("test-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
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

static void lock_handle_locked(void *data, struct ext_session_lock_v1 *lock) {
    (void)data;
    (void)lock;
    printf("[CLIENT] ext_session_lock_v1.locked event received!\n");
    locked_received = 1;
}

static void lock_handle_finished(void *data, struct ext_session_lock_v1 *lock) {
    (void)data;
    (void)lock;
    printf("[CLIENT] ext_session_lock_v1.finished event received!\n");
    finished_received = 1;
}

static const struct ext_session_lock_v1_listener lock_listener = {
    .locked = lock_handle_locked,
    .finished = lock_handle_finished,
};

static struct ext_session_lock_surface_v1 *lock_surface = NULL;
static struct wl_surface *surface = NULL;

static void lock_surface_handle_configure(void *data,
        struct ext_session_lock_surface_v1 *lock_surf,
        uint32_t serial, uint32_t width, uint32_t height) {
    (void)data;
    printf("[CLIENT] lock_surface configure received: serial=%u, %ux%u\n", serial, width, height);
    configured_received = 1;

    ext_session_lock_surface_v1_ack_configure(lock_surf, serial);

    struct wl_buffer *buffer = create_buffer(width > 0 ? width : 100, height > 0 ? height : 100);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
    .configure = lock_surface_handle_configure,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    (void)data;
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "wl_output") == 0) {
        if (!output) {
            output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        }
    } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        printf("[CLIENT] Found global: %s\n", interface);
        lock_manager = wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1);
    } else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
        printf("[CLIENT] Found global: %s\n", interface);
        idle_inhibit_manager = wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1);
    } else if (strcmp(interface, "ext_idle_notifier_v1") == 0) {
        printf("[CLIENT] Found global: %s\n", interface);
        has_idle_notify = 1;
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

int main(void) {
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!lock_manager) {
        fprintf(stderr, "Error: ext_session_lock_manager_v1 not advertised!\n");
        return 2;
    }
    if (!idle_inhibit_manager) {
        fprintf(stderr, "Error: zwp_idle_inhibit_manager_v1 not advertised!\n");
        return 3;
    }
    if (!has_idle_notify) {
        fprintf(stderr, "Error: ext_idle_notifier_v1 not advertised!\n");
        return 4;
    }
    printf("[CLIENT] Verified all 3 protocols advertised: session-lock, idle-inhibit, idle-notify\n");

    // Test 1: Idle Inhibit lifecycle
    printf("[CLIENT] Testing idle inhibitor creation...\n");
    struct wl_surface *video_surface = wl_compositor_create_surface(compositor);
    struct zwp_idle_inhibitor_v1 *inhibitor =
        zwp_idle_inhibit_manager_v1_create_inhibitor(idle_inhibit_manager, video_surface);
    wl_display_roundtrip(display);
    printf("[CLIENT] Idle inhibitor created successfully!\n");

    printf("[CLIENT] Testing idle inhibitor destruction...\n");
    zwp_idle_inhibitor_v1_destroy(inhibitor);
    wl_surface_destroy(video_surface);
    wl_display_roundtrip(display);
    printf("[CLIENT] Idle inhibitor destroyed successfully!\n");

    // Test 2: Session Lock lifecycle
    printf("[CLIENT] Testing session lock...\n");
    struct ext_session_lock_v1 *lock = ext_session_lock_manager_v1_lock(lock_manager);
    ext_session_lock_v1_add_listener(lock, &lock_listener, NULL);

    surface = wl_compositor_create_surface(compositor);
    lock_surface = ext_session_lock_v1_get_lock_surface(lock, surface, output);
    ext_session_lock_surface_v1_add_listener(lock_surface, &lock_surface_listener, NULL);

    wl_display_flush(display);

    int attempts = 0;
    while (!locked_received && attempts < 50) {
        if (wl_display_dispatch(display) < 0) {
            fprintf(stderr, "Error dispatching wayland events\n");
            return 5;
        }
        attempts++;
        usleep(10000); // 10ms
    }

    if (!locked_received) {
        fprintf(stderr, "Error: did not receive locked event after 500ms!\n");
        return 6;
    }

    printf("[CLIENT] SUCCESS: Session is securely locked!\n");

    // Unlock and destroy
    printf("[CLIENT] Testing unlock_and_destroy...\n");
    ext_session_lock_v1_unlock_and_destroy(lock);
    wl_display_roundtrip(display);

    printf("[CLIENT] Session successfully unlocked!\n");
    wl_display_disconnect(display);
    return 0;
}
