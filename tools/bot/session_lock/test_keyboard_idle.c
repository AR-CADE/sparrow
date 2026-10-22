#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "ext-idle-notify-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

static struct wl_seat *seat = NULL;
static struct ext_idle_notifier_v1 *idle_notifier = NULL;
static struct zwp_virtual_keyboard_manager_v1 *vk_manager = NULL;
static struct zwp_virtual_keyboard_v1 *vk = NULL;

static bool got_idled = false;
static bool got_resumed = false;

static void handle_idled(void *data, struct ext_idle_notification_v1 *notif)
{
    (void)data;
    (void)notif;
    printf("[TEST-KEYBOARD-IDLE] IDLED event received!\n");
    got_idled = true;
}

static void handle_resumed(void *data, struct ext_idle_notification_v1 *notif)
{
    (void)data;
    (void)notif;
    printf("[TEST-KEYBOARD-IDLE] RESUMED event received!\n");
    got_resumed = true;
}

static const struct ext_idle_notification_v1_listener notif_listener = {
    .idled = handle_idled,
    .resumed = handle_resumed,
};

static void registry_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    (void)data; (void)version;
    if (strcmp(interface, "wl_seat") == 0)
    {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    }
    else if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0)
    {
        idle_notifier = wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1);
    }
    else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0)
    {
        vk_manager = wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
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

    if (!seat || !idle_notifier || !vk_manager)
    {
        fprintf(stderr, "Missing required globals: seat=%p idle=%p vk_mgr=%p\n",
            (void*)seat, (void*)idle_notifier, (void*)vk_manager);
        return 1;
    }

    // Attach virtual keyboard
    vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vk_manager, seat);
    if (!vk)
    {
        fprintf(stderr, "Failed to create virtual keyboard\n");
        return 1;
    }

    // Provide a valid keymap
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    char *km_str = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
    size_t km_len = strlen(km_str) + 1;
    int km_fd = memfd_create("test-km", MFD_CLOEXEC);
    if (write(km_fd, km_str, km_len) == (ssize_t)km_len)
    {
        zwp_virtual_keyboard_v1_keymap(vk, 1, km_fd, km_len);
    }
    close(km_fd);
    free(km_str);
    xkb_keymap_unref(km);
    xkb_context_unref(ctx);

    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    // Register idle notification with 1000ms timeout
    printf("[TEST-KEYBOARD-IDLE] Registering idle notification (timeout = 1000ms)...\n");
    struct ext_idle_notification_v1 *notif =
        ext_idle_notifier_v1_get_idle_notification(idle_notifier, 1000, seat);
    ext_idle_notification_v1_add_listener(notif, &notif_listener, NULL);
    wl_display_roundtrip(display);

    // Phase 1: Typing simulation (send key press/release every 200ms for 2.5 seconds)
    printf("[TEST-KEYBOARD-IDLE] Phase 1: Simulating typing every 200ms for 2.5s (timeout is 1000ms)...\n");
    for (int i = 0; i < 12; i++)
    {
        uint32_t t = (i + 1) * 200;
        zwp_virtual_keyboard_v1_key(vk, t, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
        wl_display_flush(display);
        usleep(50000); // 50ms
        zwp_virtual_keyboard_v1_key(vk, t + 50, 30, WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_display_flush(display);
        usleep(150000); // 150ms

        wl_display_dispatch_pending(display);
        if (got_idled)
        {
            fprintf(stderr, "[TEST-KEYBOARD-IDLE] FAILED: IDLED event received during active typing at t=%d ms!\n", t);
            ext_idle_notification_v1_destroy(notif);
            zwp_virtual_keyboard_v1_destroy(vk);
            wl_display_disconnect(display);
            return 2;
        }
    }

    printf("[TEST-KEYBOARD-IDLE] Phase 1 PASSED: Kept awake for 2.5s during typing!\n");

    // Phase 2: Inactivity - stop typing and wait for idle event (up to 2.5s)
    printf("[TEST-KEYBOARD-IDLE] Phase 2: Stopping keystrokes, waiting for idle timeout (up to 2.5s)...\n");
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
        fprintf(stderr, "[TEST-KEYBOARD-IDLE] FAILED: IDLED event was NOT received after typing stopped!\n");
        ext_idle_notification_v1_destroy(notif);
        zwp_virtual_keyboard_v1_destroy(vk);
        wl_display_disconnect(display);
        return 3;
    }

    printf("[TEST-KEYBOARD-IDLE] Phase 2 PASSED: IDLED received as expected after inactivity!\n");

    // Phase 3: Resume test - sending a key while idled should trigger resumed
    printf("[TEST-KEYBOARD-IDLE] Phase 3: Sending keypress to verify RESUMED event...\n");
    got_resumed = false;
    zwp_virtual_keyboard_v1_key(vk, 5000, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(vk, 5050, 30, WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush(display);

    for (int i = 0; i < 10; i++)
    {
        wl_display_dispatch_pending(display);
        if (got_resumed)
        {
            break;
        }
        usleep(50000); // 50ms
        wl_display_dispatch(display);
    }

    if (!got_resumed)
    {
        fprintf(stderr, "[TEST-KEYBOARD-IDLE] FAILED: RESUMED event was NOT received upon keypress!\n");
        ext_idle_notification_v1_destroy(notif);
        zwp_virtual_keyboard_v1_destroy(vk);
        wl_display_disconnect(display);
        return 4;
    }

    printf("[TEST-KEYBOARD-IDLE] Phase 3 PASSED: RESUMED event received on keypress!\n");

    // Phase 4: Modifier key test (Shift / Ctrl modifiers keep session awake)
    printf("[TEST-KEYBOARD-IDLE] Phase 4: Simulating modifier toggles every 200ms for 2.5s...\n");
    got_idled = false;
    for (int i = 0; i < 12; i++)
    {
        uint32_t mods = (i % 2 == 0) ? 1 : 0; // Toggle Shift modifier
        zwp_virtual_keyboard_v1_modifiers(vk, mods, 0, 0, 0);
        wl_display_flush(display);
        usleep(200000); // 200ms

        wl_display_dispatch_pending(display);
        if (got_idled)
        {
            fprintf(stderr, "[TEST-KEYBOARD-IDLE] FAILED: IDLED event received during modifier changes at step %d!\n", i);
            ext_idle_notification_v1_destroy(notif);
            zwp_virtual_keyboard_v1_destroy(vk);
            wl_display_disconnect(display);
            return 5;
        }
    }

    printf("[TEST-KEYBOARD-IDLE] Phase 4 PASSED: Kept awake by modifier activity alone!\n");
    printf("[TEST-KEYBOARD-IDLE] ALL TESTS PASSED SUCCESSFULLY!\n");

    ext_idle_notification_v1_destroy(notif);
    zwp_virtual_keyboard_v1_destroy(vk);
    wl_display_disconnect(display);
    return 0;
}
