#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "virtual-keyboard-unstable-v1-client-protocol.h"

struct vk_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct zwp_virtual_keyboard_manager_v1 *vk_mgr;
    struct zwp_virtual_keyboard_v1 *vk;
    uint32_t seat_caps;
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    (void)seat;
    struct vk_app *app = data;
    app->seat_caps = caps;
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version) {
    struct vk_app *app = data;
    (void)version;
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        if (!app->seat) {
            app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
            wl_seat_add_listener(app->seat, &seat_listener, app);
        }
    } else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        app->vk_mgr = wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static uint32_t name_to_keycode(const char *name) {
    if (!strcasecmp(name, "F1")) return KEY_F1;
    if (!strcasecmp(name, "F2")) return KEY_F2;
    if (!strcasecmp(name, "F3")) return KEY_F3;
    if (!strcasecmp(name, "F4")) return KEY_F4;
    if (!strcasecmp(name, "F5")) return KEY_F5;
    if (!strcasecmp(name, "F6")) return KEY_F6;
    if (!strcasecmp(name, "F7")) return KEY_F7;
    if (!strcasecmp(name, "F8")) return KEY_F8;
    if (!strcasecmp(name, "F9")) return KEY_F9;
    if (!strcasecmp(name, "F10")) return KEY_F10;
    if (!strcasecmp(name, "F11")) return KEY_F11;
    if (!strcasecmp(name, "F12")) return KEY_F12;

    if (!strcasecmp(name, "Alt_L") || !strcasecmp(name, "Alt") || !strcasecmp(name, "AltLeft") || !strcasecmp(name, "LeftAlt"))
        return KEY_LEFTALT;
    if (!strcasecmp(name, "Alt_R") || !strcasecmp(name, "AltRight") || !strcasecmp(name, "RightAlt"))
        return KEY_RIGHTALT;

    if (!strcasecmp(name, "Control_L") || !strcasecmp(name, "Ctrl") || !strcasecmp(name, "Ctrl_L") || !strcasecmp(name, "Control"))
        return KEY_LEFTCTRL;
    if (!strcasecmp(name, "Control_R") || !strcasecmp(name, "Ctrl_R"))
        return KEY_RIGHTCTRL;

    if (!strcasecmp(name, "Shift_L") || !strcasecmp(name, "Shift"))
        return KEY_LEFTSHIFT;
    if (!strcasecmp(name, "Shift_R"))
        return KEY_RIGHTSHIFT;

    if (!strcasecmp(name, "Super") || !strcasecmp(name, "Super_L") || !strcasecmp(name, "Meta") || !strcasecmp(name, "Win"))
        return KEY_LEFTMETA;

    if (!strcasecmp(name, "Return") || !strcasecmp(name, "Enter"))
        return KEY_ENTER;
    if (!strcasecmp(name, "Escape") || !strcasecmp(name, "Esc"))
        return KEY_ESC;
    if (!strcasecmp(name, "Tab"))
        return KEY_TAB;
    if (!strcasecmp(name, "Space"))
        return KEY_SPACE;
    if (!strcasecmp(name, "BackSpace") || !strcasecmp(name, "Backspace"))
        return KEY_BACKSPACE;

    if (!strcasecmp(name, "Left")) return KEY_LEFT;
    if (!strcasecmp(name, "Right")) return KEY_RIGHT;
    if (!strcasecmp(name, "Up")) return KEY_UP;
    if (!strcasecmp(name, "Down")) return KEY_DOWN;

    if (!strcasecmp(name, "Page_Up") || !strcasecmp(name, "PageUp")) return KEY_PAGEUP;
    if (!strcasecmp(name, "Page_Down") || !strcasecmp(name, "PageDown")) return KEY_PAGEDOWN;
    if (!strcasecmp(name, "Home")) return KEY_HOME;
    if (!strcasecmp(name, "End")) return KEY_END;

    // Check if direct integer keycode
    if (name[0] >= '0' && name[0] <= '9') {
        return (uint32_t)atoi(name);
    }

    return 0;
}

static bool char_to_keycode(char c, uint32_t *keycode, bool *shift) {
    *shift = false;
    if (c >= 'a' && c <= 'z') {
        static const uint32_t alpha_keys[26] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
            KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
            KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
        };
        *keycode = alpha_keys[c - 'a'];
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        static const uint32_t alpha_keys[26] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
            KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
            KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
        };
        *keycode = alpha_keys[c - 'A'];
        *shift = true;
        return true;
    }
    if (c >= '1' && c <= '9') {
        *keycode = KEY_1 + (c - '1');
        return true;
    }
    if (c == '0') {
        *keycode = KEY_0;
        return true;
    }

    switch (c) {
        case '\n': *keycode = KEY_ENTER; return true;
        case '\t': *keycode = KEY_TAB; return true;
        case ' ':  *keycode = KEY_SPACE; return true;
        case '-':  *keycode = KEY_MINUS; return true;
        case '_':  *keycode = KEY_MINUS; *shift = true; return true;
        case '=':  *keycode = KEY_EQUAL; return true;
        case '+':  *keycode = KEY_EQUAL; *shift = true; return true;
        case '/':  *keycode = KEY_SLASH; return true;
        case '?':  *keycode = KEY_SLASH; *shift = true; return true;
        case '.':  *keycode = KEY_DOT; return true;
        case '>':  *keycode = KEY_DOT; *shift = true; return true;
        case ',':  *keycode = KEY_COMMA; return true;
        case '<':  *keycode = KEY_COMMA; *shift = true; return true;
        case ';':  *keycode = KEY_SEMICOLON; return true;
        case ':':  *keycode = KEY_SEMICOLON; *shift = true; return true;
        case '\'': *keycode = KEY_APOSTROPHE; return true;
        case '"':  *keycode = KEY_APOSTROPHE; *shift = true; return true;
        case '`':  *keycode = KEY_GRAVE; return true;
        case '~':  *keycode = KEY_GRAVE; *shift = true; return true;
        case '[':  *keycode = KEY_LEFTBRACE; return true;
        case '{':  *keycode = KEY_LEFTBRACE; *shift = true; return true;
        case ']':  *keycode = KEY_RIGHTBRACE; return true;
        case '}':  *keycode = KEY_RIGHTBRACE; *shift = true; return true;
        case '\\': *keycode = KEY_BACKSLASH; return true;
        case '|':  *keycode = KEY_BACKSLASH; *shift = true; return true;
        case '!':  *keycode = KEY_1; *shift = true; return true;
        case '@':  *keycode = KEY_2; *shift = true; return true;
        case '#':  *keycode = KEY_3; *shift = true; return true;
        case '$':  *keycode = KEY_4; *shift = true; return true;
        case '%':  *keycode = KEY_5; *shift = true; return true;
        case '^':  *keycode = KEY_6; *shift = true; return true;
        case '&':  *keycode = KEY_7; *shift = true; return true;
        case '*':  *keycode = KEY_8; *shift = true; return true;
        case '(':  *keycode = KEY_9; *shift = true; return true;
        case ')':  *keycode = KEY_0; *shift = true; return true;
        default: return false;
    }
}

static void send_raw_key(struct vk_app *app, uint32_t keycode, uint32_t state) {
    static uint32_t ts = 100;
    ts += 20;
    zwp_virtual_keyboard_v1_key(app->vk, ts, keycode, state);
    wl_display_flush(app->display);
}

static void tap_keycode(struct vk_app *app, uint32_t keycode) {
    send_raw_key(app, keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
    usleep(15000);
    send_raw_key(app, keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
    usleep(15000);
}

static void type_text(struct vk_app *app, const char *text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        // Handle escaped \n or \t
        char c = text[i];
        if (c == '\\' && text[i + 1] != '\0') {
            if (text[i + 1] == 'n') {
                c = '\n';
                i++;
            } else if (text[i + 1] == 't') {
                c = '\t';
                i++;
            }
        }

        uint32_t keycode = 0;
        bool shift = false;
        if (char_to_keycode(c, &keycode, &shift)) {
            if (shift) {
                send_raw_key(app, KEY_LEFTSHIFT, WL_KEYBOARD_KEY_STATE_PRESSED);
                usleep(5000);
            }
            tap_keycode(app, keycode);
            if (shift) {
                send_raw_key(app, KEY_LEFTSHIFT, WL_KEYBOARD_KEY_STATE_RELEASED);
                usleep(5000);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <key...>\n", argv[0]);
        fprintf(stderr, "       %s type \"<text>\"\n", argv[0]);
        fprintf(stderr, "       %s combo <mod> <key>\n", argv[0]);
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s F11\n", argv[0]);
        fprintf(stderr, "  %s F12\n", argv[0]);
        fprintf(stderr, "  %s Alt_L\n", argv[0]);
        fprintf(stderr, "  %s Return\n", argv[0]);
        fprintf(stderr, "  %s type \"uname -a\\n\"\n", argv[0]);
        fprintf(stderr, "  %s combo Alt_L Tab\n", argv[0]);
        return 1;
    }

    struct vk_app app = {0};
    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "[sparrow_key] Error: Cannot connect to Wayland display\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (!app.seat || !app.vk_mgr) {
        fprintf(stderr, "[sparrow_key] Error: Missing wl_seat or zwp_virtual_keyboard_manager_v1\n");
        wl_display_disconnect(app.display);
        return 1;
    }

    app.vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(app.vk_mgr, app.seat);
    if (!app.vk) {
        fprintf(stderr, "[sparrow_key] Error: Failed to create virtual keyboard\n");
        wl_display_disconnect(app.display);
        return 1;
    }

    // Initialize valid standard XKB keymap
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    char *km_str = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
    size_t km_len = strlen(km_str) + 1;
    int km_fd = memfd_create("sparrow-keymap", MFD_CLOEXEC);
    if (write(km_fd, km_str, km_len) == (ssize_t)km_len) {
        zwp_virtual_keyboard_v1_keymap(app.vk, 1, km_fd, km_len);
    }
    close(km_fd);
    free(km_str);
    xkb_keymap_unref(km);
    xkb_context_unref(ctx);

    wl_display_roundtrip(app.display);

    if (strcmp(argv[1], "type") == 0) {
        for (int i = 2; i < argc; i++) {
            type_text(&app, argv[i]);
            if (i + 1 < argc) {
                tap_keycode(&app, KEY_SPACE);
            }
        }
    } else if (strcmp(argv[1], "combo") == 0) {
        if (argc >= 4) {
            uint32_t mod = name_to_keycode(argv[2]);
            uint32_t key = name_to_keycode(argv[3]);
            if (mod && key) {
                send_raw_key(&app, mod, WL_KEYBOARD_KEY_STATE_PRESSED);
                usleep(15000);
                tap_keycode(&app, key);
                send_raw_key(&app, mod, WL_KEYBOARD_KEY_STATE_RELEASED);
                usleep(15000);
            }
        }
    } else {
        for (int i = 1; i < argc; i++) {
            uint32_t code = name_to_keycode(argv[i]);
            if (code != 0) {
                tap_keycode(&app, code);
            } else {
                fprintf(stderr, "[sparrow_key] Warning: Unknown key '%s'\n", argv[i]);
            }
        }
    }

    wl_display_roundtrip(app.display);
    zwp_virtual_keyboard_v1_destroy(app.vk);
    zwp_virtual_keyboard_manager_v1_destroy(app.vk_mgr);
    wl_seat_destroy(app.seat);
    wl_registry_destroy(app.registry);
    wl_display_disconnect(app.display);

    return 0;
}
