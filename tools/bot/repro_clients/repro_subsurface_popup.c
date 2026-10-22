#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

// Basic 8x8 font for ASCII 32..126
static const unsigned char font8x8[95][8] = {
    [' ' - 32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['(' - 32] = {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00},
    [')' - 32] = {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00},
    ['[' - 32] = {0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00},
    [']' - 32] = {0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00},
    ['v' - 32] = {0x00,0x00,0x66,0x66,0x66,0x3c,0x18,0x00},
    ['X' - 32] = {0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0x00},
    ['A' - 32] = {0x18,0x3c,0x66,0x7e,0x66,0x66,0x66,0x00},
    ['C' - 32] = {0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00},
    ['E' - 32] = {0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0x00},
    ['L' - 32] = {0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0x00},
    ['N' - 32] = {0x66,0x76,0x7e,0x7e,0x6e,0x66,0x66,0x00},
    ['O' - 32] = {0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00},
    ['P' - 32] = {0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0x00},
    ['S' - 32] = {0x3c,0x66,0x60,0x3c,0x06,0x66,0x3c,0x00},
    ['U' - 32] = {0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0x00},
};

static void draw_text(uint32_t *data, int width, int height, int start_x, int start_y,
                      const char *str, uint32_t color) {
    int cx = start_x;
    int cy = start_y;
    for (size_t i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c < 32 || c > 126) c = ' ';
        const unsigned char *glyph = font8x8[c - 32];
        for (int row = 0; row < 8; row++) {
            unsigned char line = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (line & (1 << (7 - col))) {
                    int px = cx + col;
                    int py = cy + row;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        data[py * width + px] = color;
                    }
                }
            }
        }
        cx += 10;
    }
}

#define BUTTON_X 80
#define BUTTON_Y 40
#define BUTTON_W 220
#define BUTTON_H 44

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct wl_pointer *pointer;

    // Toplevel
    struct wl_surface *top_surface;
    struct xdg_surface *top_xdg_surface;
    struct xdg_toplevel *top_xdg_toplevel;
    struct wl_buffer *top_buf;
    uint32_t *top_pixels;
    int top_fd;
    size_t top_size;

    // Popup
    struct wl_surface *popup_surface;
    struct xdg_surface *popup_xdg_surface;
    struct xdg_popup *popup_xdg_popup;
    struct wl_buffer *popup_buf;

    // Child Subsurface on Popup
    struct wl_surface *sub_surface;
    struct wl_subsurface *wl_subsurface;
    struct wl_buffer *sub_buf;

    bool popup_open;
    double pointer_x;
    double pointer_y;
    bool running;
};

static int create_shm_file(off_t size) {
    int fd = memfd_create("sparrow-subsurface-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static struct wl_buffer *create_shm_buffer(struct wl_shm *shm, int width, int height,
                                           uint32_t fill_color, uint32_t border_color) {
    int stride = width * 4;
    size_t size = stride * height;
    int fd = create_shm_file(size);
    if (fd < 0) return NULL;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (x == 0 || x == width - 1 || y == 0 || y == height - 1) {
                data[y * width + x] = border_color;
            } else {
                data[y * width + x] = fill_color;
            }
        }
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);

    return buffer;
}

static void redraw_toplevel_button(struct app_state *app) {
    if (!app->top_pixels) return;

    // Redraw button area
    uint32_t btn_fill = app->popup_open ? 0xFFC04020 : 0xFF0099CC;
    uint32_t btn_border = 0xFFFFFFFF;
    const char *label = app->popup_open ? "[ CLOSE POPUP (X) ]" : "[ OPEN POPUP (v) ]";

    for (int y = BUTTON_Y; y < BUTTON_Y + BUTTON_H; y++) {
        for (int x = BUTTON_X; x < BUTTON_X + BUTTON_W; x++) {
            if (x == BUTTON_X || x == BUTTON_X + BUTTON_W - 1 || y == BUTTON_Y || y == BUTTON_Y + BUTTON_H - 1) {
                app->top_pixels[y * 640 + x] = btn_border;
            } else {
                app->top_pixels[y * 640 + x] = btn_fill;
            }
        }
    }

    draw_text(app->top_pixels, 640, 480, BUTTON_X + 16, BUTTON_Y + 18, label, 0xFFFFFFFF);

    wl_surface_attach(app->top_surface, app->top_buf, 0, 0);
    wl_surface_damage_buffer(app->top_surface, BUTTON_X, BUTTON_Y, BUTTON_W, BUTTON_H);
    wl_surface_commit(app->top_surface);
    wl_display_flush(app->display);
}

static void close_popup(struct app_state *app) {
    if (!app->popup_open) return;

    if (app->wl_subsurface) {
        wl_subsurface_destroy(app->wl_subsurface);
        app->wl_subsurface = NULL;
    }
    if (app->sub_surface) {
        wl_surface_destroy(app->sub_surface);
        app->sub_surface = NULL;
    }
    if (app->sub_buf) {
        wl_buffer_destroy(app->sub_buf);
        app->sub_buf = NULL;
    }

    if (app->popup_xdg_popup) {
        xdg_popup_destroy(app->popup_xdg_popup);
        app->popup_xdg_popup = NULL;
    }
    if (app->popup_xdg_surface) {
        xdg_surface_destroy(app->popup_xdg_surface);
        app->popup_xdg_surface = NULL;
    }
    if (app->popup_surface) {
        wl_surface_destroy(app->popup_surface);
        app->popup_surface = NULL;
    }
    if (app->popup_buf) {
        wl_buffer_destroy(app->popup_buf);
        app->popup_buf = NULL;
    }

    app->popup_open = false;
    printf("[REPRO-SUBSURFACE-POPUP] Popup closed via button/dismissal\n");
    fflush(stdout);
    redraw_toplevel_button(app);
}

static void popup_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial);
static void popup_xdg_popup_configure(void *data, struct xdg_popup *popup,
                                      int32_t x, int32_t y, int32_t width, int32_t height);
static void popup_xdg_popup_done(void *data, struct xdg_popup *popup);

static const struct xdg_surface_listener popup_xdg_surface_listener = {
    .configure = popup_xdg_surface_configure,
};

static const struct xdg_popup_listener popup_xdg_popup_listener = {
    .configure = popup_xdg_popup_configure,
    .popup_done = popup_xdg_popup_done,
    .repositioned = NULL,
};

static void open_popup(struct app_state *app) {
    if (app->popup_open) return;

    // Create positioner anchored right under the button
    struct xdg_positioner *pos = xdg_wm_base_create_positioner(app->wm_base);
    xdg_positioner_set_size(pos, 300, 200);
    xdg_positioner_set_anchor_rect(pos, BUTTON_X, BUTTON_Y + BUTTON_H + 4, BUTTON_W, 1);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);

    app->popup_surface = wl_compositor_create_surface(app->compositor);
    app->popup_xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->popup_surface);
    xdg_surface_add_listener(app->popup_xdg_surface, &popup_xdg_surface_listener, app);

    app->popup_xdg_popup = xdg_surface_get_popup(app->popup_xdg_surface, app->top_xdg_surface, pos);
    xdg_popup_add_listener(app->popup_xdg_popup, &popup_xdg_popup_listener, app);
    xdg_positioner_destroy(pos);

    wl_surface_commit(app->popup_surface);
    wl_display_roundtrip(app->display);

    app->popup_buf = create_shm_buffer(app->shm, 300, 200, 0xFF335577, 0xFFFFFFFF);
    wl_surface_attach(app->popup_surface, app->popup_buf, 0, 0);
    wl_surface_damage_buffer(app->popup_surface, 0, 0, 300, 200);
    wl_surface_commit(app->popup_surface);
    wl_display_roundtrip(app->display);

    // Attach child subsurface on popup
    app->sub_surface = wl_compositor_create_surface(app->compositor);
    app->wl_subsurface = wl_subcompositor_get_subsurface(app->subcompositor,
                                                        app->sub_surface,
                                                        app->popup_surface);
    wl_subsurface_set_position(app->wl_subsurface, 20, 20);
    wl_subsurface_set_desync(app->wl_subsurface);

    app->sub_buf = create_shm_buffer(app->shm, 140, 60, 0xFFE05533, 0xFFFFFF00);
    wl_surface_attach(app->sub_surface, app->sub_buf, 0, 0);
    wl_surface_damage_buffer(app->sub_surface, 0, 0, 140, 60);
    wl_surface_commit(app->sub_surface);

    // Re-commit parent popup to apply subsurface hierarchy
    wl_surface_commit(app->popup_surface);
    wl_display_roundtrip(app->display);

    app->popup_open = true;
    printf("[REPRO-SUBSURFACE-POPUP] Popup opened with child subsurface (pos=20,20 size=140x60)\n");
    fflush(stdout);
    redraw_toplevel_button(app);
}

static void toggle_popup(struct app_state *app) {
    if (app->popup_open) {
        close_popup(app);
    } else {
        open_popup(app);
    }
}

static void popup_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static void popup_xdg_popup_configure(void *data, struct xdg_popup *popup,
                                      int32_t x, int32_t y, int32_t width, int32_t height) {
    (void)data; (void)popup; (void)x; (void)y; (void)width; (void)height;
}

static void popup_xdg_popup_done(void *data, struct xdg_popup *popup) {
    (void)popup;
    struct app_state *app = data;
    close_popup(app);
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void top_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener top_xdg_surface_listener = {
    .configure = top_xdg_surface_configure,
};

static void top_xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                       int32_t width, int32_t height, struct wl_array *states) {
    (void)data; (void)toplevel; (void)width; (void)height; (void)states;
}

static void top_xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)toplevel;
    struct app_state *app = data;
    app->running = false;
}

static const struct xdg_toplevel_listener top_xdg_toplevel_listener = {
    .configure = top_xdg_toplevel_configure,
    .close = top_xdg_toplevel_close,
};

// Pointer event handlers for interactive button clicking
static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)serial; (void)surface;
    struct app_state *app = data;
    app->pointer_x = wl_fixed_to_double(sx);
    app->pointer_y = wl_fixed_to_double(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)time;
    struct app_state *app = data;
    app->pointer_x = wl_fixed_to_double(sx);
    app->pointer_y = wl_fixed_to_double(sy);
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer; (void)serial; (void)time;
    struct app_state *app = data;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED && (button == BTN_LEFT || button == 0x110)) {
        if (app->pointer_x >= BUTTON_X && app->pointer_x <= (BUTTON_X + BUTTON_W) &&
            app->pointer_y >= BUTTON_Y && app->pointer_y <= (BUTTON_Y + BUTTON_H)) {
            printf("[REPRO-SUBSURFACE-POPUP] Click detected on button (x=%.1f, y=%.1f) -> Toggling popup\n",
                   app->pointer_x, app->pointer_y);
            fflush(stdout);
            toggle_popup(app);
        }
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    struct app_state *app = data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    }
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
    struct app_state *app = data;
    (void)version;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        app->subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        if (!app->seat) {
            app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
            wl_seat_add_listener(app->seat, &seat_listener, app);
        }
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int main(int argc, char **argv) {
    int duration_sec = 10;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--duration=", 11) == 0) {
            duration_sec = atoi(argv[i] + 11);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration_sec = atoi(argv[++i]);
        }
    }

    struct app_state app = { .running = true, .popup_open = false };

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "[FAIL] Cannot connect to Wayland display\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.subcompositor || !app.shm || !app.wm_base) {
        fprintf(stderr, "[FAIL] Missing required Wayland interfaces\n");
        return 1;
    }

    // 1. Create Toplevel
    app.top_surface = wl_compositor_create_surface(app.compositor);
    app.top_xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.top_surface);
    xdg_surface_add_listener(app.top_xdg_surface, &top_xdg_surface_listener, &app);

    app.top_xdg_toplevel = xdg_surface_get_toplevel(app.top_xdg_surface);
    xdg_toplevel_add_listener(app.top_xdg_toplevel, &top_xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.top_xdg_toplevel, "Sparrow Repro - Subsurface Popup");
    xdg_toplevel_set_app_id(app.top_xdg_toplevel, "sparrow.repro.subsurface_popup");

    wl_surface_commit(app.top_surface);
    wl_display_roundtrip(app.display);

    // Allocate persistent toplevel SHM buffer
    int stride = 640 * 4;
    app.top_size = stride * 480;
    app.top_fd = create_shm_file(app.top_size);
    app.top_pixels = mmap(NULL, app.top_size, PROT_READ | PROT_WRITE, MAP_SHARED, app.top_fd, 0);

    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 640; x++) {
            if (x == 0 || x == 639 || y == 0 || y == 479) {
                app.top_pixels[y * 640 + x] = 0xFFFFFFFF;
            } else {
                app.top_pixels[y * 640 + x] = 0xFF1C2836;
            }
        }
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(app.shm, app.top_fd, app.top_size);
    app.top_buf = wl_shm_pool_create_buffer(pool, 0, 640, 480, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    // Initial popup creation (starts open)
    open_popup(&app);

    printf("[REPRO-SUBSURFACE-POPUP] Interactive button ready at (x=%d..%d, y=%d..%d)\n",
           BUTTON_X, BUTTON_X + BUTTON_W, BUTTON_Y, BUTTON_Y + BUTTON_H);
    fflush(stdout);

    time_t start = time(NULL);
    while (app.running && (duration_sec <= 0 || (time(NULL) - start < duration_sec))) {
        if (wl_display_dispatch(app.display) < 0) {
            break;
        }
    }

    printf("[REPRO-SUBSURFACE-POPUP] Finished execution successfully\n");

    close_popup(&app);

    if (app.pointer) wl_pointer_destroy(app.pointer);
    if (app.seat) wl_seat_destroy(app.seat);

    xdg_toplevel_destroy(app.top_xdg_toplevel);
    xdg_surface_destroy(app.top_xdg_surface);
    wl_surface_destroy(app.top_surface);
    if (app.top_buf) wl_buffer_destroy(app.top_buf);
    if (app.top_pixels) munmap(app.top_pixels, app.top_size);
    if (app.top_fd >= 0) close(app.top_fd);

    xdg_wm_base_destroy(app.wm_base);
    wl_shm_destroy(app.shm);
    wl_subcompositor_destroy(app.subcompositor);
    wl_compositor_destroy(app.compositor);
    wl_registry_destroy(app.registry);
    wl_display_disconnect(app.display);

    return 0;
}
