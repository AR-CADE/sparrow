#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "ext-session-lock-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;
static struct wl_output *output = NULL;
static struct wl_seat *seat = NULL;
static struct wl_keyboard *keyboard = NULL;
static struct ext_session_lock_manager_v1 *lock_manager = NULL;
static struct zwp_virtual_keyboard_manager_v1 *vk_manager = NULL;
static struct zwp_virtual_keyboard_v1 *vk = NULL;

static int locked_received = 0;
static int keyboard_enter_received = 0;
static int keyboard_key_received = 0;
static struct wl_surface *keyboard_entered_surface = NULL;

static int create_shm_file(off_t size) {
    int fd = memfd_create("test-focus-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
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

static void keyboard_handle_keymap(void *data, struct wl_keyboard *kb, uint32_t format, int fd, uint32_t size) {
    (void)data; (void)kb; (void)format; (void)size;
    close(fd);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *surf, struct wl_array *keys) {
    (void)data; (void)kb; (void)serial; (void)keys;
    printf("[LOCK-CLIENT] wl_keyboard.enter received for surface %p!\n", (void*)surf);
    keyboard_enter_received = 1;
    keyboard_entered_surface = surf;
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *surf) {
    (void)data; (void)kb; (void)serial; (void)surf;
    printf("[LOCK-CLIENT] wl_keyboard.leave received for surface %p\n", (void*)surf);
}

static void keyboard_handle_key(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    (void)data; (void)kb; (void)serial; (void)time;
    printf("[LOCK-CLIENT] wl_keyboard.key received: key=%u, state=%u\n", key, state);
    keyboard_key_received++;
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    (void)data; (void)kb; (void)serial; (void)mods_depressed; (void)mods_latched; (void)mods_locked; (void)group;
}

static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *kb, int32_t rate, int32_t delay) {
    (void)data; (void)kb; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

static void seat_handle_capabilities(void *data, struct wl_seat *s, uint32_t caps) {
    (void)data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
        printf("[LOCK-CLIENT] Seat has keyboard capability, getting wl_keyboard...\n");
        keyboard = wl_seat_get_keyboard(s);
        wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    }
}

static void seat_handle_name(void *data, struct wl_seat *s, const char *name) {
    (void)data; (void)s; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void lock_handle_locked(void *data, struct ext_session_lock_v1 *lock) {
    (void)data; (void)lock;
    printf("[LOCK-CLIENT] ext_session_lock_v1.locked received!\n");
    locked_received = 1;
}

static void lock_handle_finished(void *data, struct ext_session_lock_v1 *lock) {
    (void)data; (void)lock;
    printf("[LOCK-CLIENT] ext_session_lock_v1.finished received\n");
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
    printf("[LOCK-CLIENT] lock_surface configure: serial=%u, %ux%u\n", serial, width, height);

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
    } else if (strcmp(interface, "wl_seat") == 0) {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        lock_manager = wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1);
    } else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        vk_manager = wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
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

    if (!lock_manager || !seat || !vk_manager) {
        fprintf(stderr, "Error: required globals (lock_manager=%p, seat=%p, vk_manager=%p) not ready\n",
            (void*)lock_manager, (void*)seat, (void*)vk_manager);
        return 2;
    }

    // Attach a virtual keyboard so the seat gains keyboard capability
    printf("[LOCK-CLIENT] Creating virtual keyboard...\n");
    vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vk_manager, seat);

    // Provide a valid keymap
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    char *km_str = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
    size_t km_len = strlen(km_str) + 1;
    int km_fd = memfd_create("test-km", MFD_CLOEXEC);
    if (write(km_fd, km_str, km_len) == (ssize_t)km_len) {
        zwp_virtual_keyboard_v1_keymap(vk, 1, km_fd, km_len);
    }
    close(km_fd);
    free(km_str);
    xkb_keymap_unref(km);
    xkb_context_unref(ctx);

    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    if (!keyboard) {
        fprintf(stderr, "Error: wl_keyboard was not created on seat\n");
        return 3;
    }

    printf("[LOCK-CLIENT] Acquiring session lock...\n");
    struct ext_session_lock_v1 *lock = ext_session_lock_manager_v1_lock(lock_manager);
    ext_session_lock_v1_add_listener(lock, &lock_listener, NULL);

    surface = wl_compositor_create_surface(compositor);
    lock_surface = ext_session_lock_v1_get_lock_surface(lock, surface, output);
    ext_session_lock_surface_v1_add_listener(lock_surface, &lock_surface_listener, NULL);

    wl_display_flush(display);

    int attempts = 0;
    while ((!locked_received || !keyboard_enter_received) && attempts < 100) {
        if (wl_display_dispatch(display) < 0) {
            fprintf(stderr, "Error dispatching wayland events\n");
            return 4;
        }
        attempts++;
        usleep(10000);
    }

    if (!locked_received) {
        fprintf(stderr, "Error: Did not receive locked event\n");
        return 5;
    }

    if (!keyboard_enter_received) {
        fprintf(stderr, "Error: Did not receive keyboard.enter on lock surface!\n");
        return 6;
    }

    if (keyboard_entered_surface != surface) {
        fprintf(stderr, "Error: Keyboard entered unexpected surface (%p vs %p)\n",
            (void*)keyboard_entered_surface, (void*)surface);
        return 7;
    }

    printf("[LOCK-CLIENT] SUCCESS: Session is locked AND lock surface holds keyboard focus!\n");

    // Send a key press and release via virtual keyboard to test key delivery to lock surface
    printf("[LOCK-CLIENT] Simulating key event via virtual keyboard (key 30 - 'A')...\n");
    zwp_virtual_keyboard_v1_key(vk, 1000, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(vk, 1050, 30, WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush(display);

    attempts = 0;
    while (keyboard_key_received < 2 && attempts < 50) {
        if (wl_display_dispatch(display) < 0) break;
        attempts++;
        usleep(10000);
    }

    if (keyboard_key_received >= 2) {
        printf("[LOCK-CLIENT] SUCCESS: Received %d keyboard events directly on lock surface!\n", keyboard_key_received);
    } else {
        fprintf(stderr, "Warning: Expected 2 key events, got %d\n", keyboard_key_received);
        return 8;
    }

    ext_session_lock_v1_unlock_and_destroy(lock);
    wl_display_roundtrip(display);

    zwp_virtual_keyboard_v1_destroy(vk);
    wl_surface_destroy(surface);
    wl_keyboard_destroy(keyboard);
    wl_seat_destroy(seat);
    wl_display_disconnect(display);
    printf("[LOCK-CLIENT] Clean exit.\n");
    return 0;
}
