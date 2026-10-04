#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <poll.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

// Basic 8x8 font for ASCII 32..126
static const unsigned char font8x8[95][8] = {
    [' ' - 32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!' - 32] = {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    ['"' - 32] = {0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
    ['#' - 32] = {0x66,0x66,0xff,0x66,0xff,0x66,0x66,0x00},
    ['$' - 32] = {0x18,0x3e,0x60,0x3c,0x06,0x7c,0x18,0x00},
    ['%' - 32] = {0x62,0x66,0x0c,0x18,0x30,0x66,0x46,0x00},
    ['&' - 32] = {0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00},
    ['\''- 32] = {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    ['(' - 32] = {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00},
    [')' - 32] = {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00},
    ['*' - 32] = {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00},
    ['+' - 32] = {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00},
    [',' - 32] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    ['-' - 32] = {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00},
    ['.' - 32] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    ['/' - 32] = {0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0x00},
    ['0' - 32] = {0x3c,0x66,0x6e,0x76,0x66,0x66,0x3c,0x00},
    ['1' - 32] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00},
    ['2' - 32] = {0x3c,0x66,0x06,0x0c,0x18,0x30,0x7e,0x00},
    ['3' - 32] = {0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00},
    ['4' - 32] = {0x0c,0x1c,0x34,0x64,0x7e,0x04,0x0e,0x00},
    ['5' - 32] = {0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00},
    ['6' - 32] = {0x3c,0x66,0x60,0x7c,0x66,0x66,0x3c,0x00},
    ['7' - 32] = {0x7e,0x66,0x0c,0x18,0x18,0x18,0x18,0x00},
    ['8' - 32] = {0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00},
    ['9' - 32] = {0x3c,0x66,0x66,0x3e,0x06,0x66,0x3c,0x00},
    [':' - 32] = {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    [';' - 32] = {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00},
    ['<' - 32] = {0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x00},
    ['=' - 32] = {0x00,0x7e,0x00,0x00,0x7e,0x00,0x00,0x00},
    ['>' - 32] = {0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x00},
    ['?' - 32] = {0x3c,0x66,0x06,0x0c,0x18,0x00,0x18,0x00},
    ['@' - 32] = {0x3c,0x66,0x6e,0x6e,0x60,0x62,0x3c,0x00},
    ['A' - 32] = {0x18,0x3c,0x66,0x7e,0x66,0x66,0x66,0x00},
    ['B' - 32] = {0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0x00},
    ['C' - 32] = {0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00},
    ['D' - 32] = {0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0x00},
    ['E' - 32] = {0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0x00},
    ['F' - 32] = {0x7e,0x60,0x60,0x7c,0x60,0x60,0x60,0x00},
    ['G' - 32] = {0x3c,0x66,0x60,0x6e,0x66,0x66,0x3a,0x00},
    ['H' - 32] = {0x66,0x66,0x66,0x7e,0x66,0x66,0x66,0x00},
    ['I' - 32] = {0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0x00},
    ['J' - 32] = {0x1e,0x0c,0x0c,0x0c,0x0c,0x6c,0x38,0x00},
    ['K' - 32] = {0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0x00},
    ['L' - 32] = {0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0x00},
    ['M' - 32] = {0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0x00},
    ['N' - 32] = {0x66,0x76,0x7e,0x7e,0x6e,0x66,0x66,0x00},
    ['O' - 32] = {0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00},
    ['P' - 32] = {0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0x00},
    ['Q' - 32] = {0x3c,0x66,0x66,0x66,0x6a,0x6c,0x36,0x00},
    ['R' - 32] = {0x7c,0x66,0x66,0x7c,0x6c,0x66,0x66,0x00},
    ['S' - 32] = {0x3c,0x66,0x60,0x3c,0x06,0x66,0x3c,0x00},
    ['T' - 32] = {0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    ['U' - 32] = {0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0x00},
    ['V' - 32] = {0x66,0x66,0x66,0x66,0x66,0x3c,0x18,0x00},
    ['W' - 32] = {0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00},
    ['X' - 32] = {0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0x00},
    ['Y' - 32] = {0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0x00},
    ['Z' - 32] = {0x7e,0x06,0x0c,0x18,0x30,0x60,0x7e,0x00},
    ['[' - 32] = {0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00},
    ['\\'- 32] = {0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0x00},
    [']' - 32] = {0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00},
    ['^' - 32] = {0x18,0x3c,0x66,0x00,0x00,0x00,0x00,0x00},
    ['_' - 32] = {0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00},
    ['`' - 32] = {0x30,0x18,0x0c,0x00,0x00,0x00,0x00,0x00},
    ['a' - 32] = {0x00,0x00,0x3c,0x06,0x3e,0x66,0x3e,0x00},
    ['b' - 32] = {0x60,0x60,0x7c,0x66,0x66,0x66,0x7c,0x00},
    ['c' - 32] = {0x00,0x00,0x3c,0x66,0x60,0x66,0x3c,0x00},
    ['d' - 32] = {0x06,0x06,0x3e,0x66,0x66,0x66,0x3e,0x00},
    ['e' - 32] = {0x00,0x00,0x3c,0x66,0x7e,0x60,0x3c,0x00},
    ['f' - 32] = {0x1c,0x30,0x30,0x7c,0x30,0x30,0x30,0x00},
    ['g' - 32] = {0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x3c},
    ['h' - 32] = {0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0x00},
    ['i' - 32] = {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00},
    ['j' - 32] = {0x06,0x00,0x0e,0x06,0x06,0x06,0x66,0x3c},
    ['k' - 32] = {0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0x00},
    ['l' - 32] = {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00},
    ['m' - 32] = {0x00,0x00,0x66,0x7f,0x7f,0x6b,0x63,0x00},
    ['n' - 32] = {0x00,0x00,0x7c,0x66,0x66,0x66,0x66,0x00},
    ['o' - 32] = {0x00,0x00,0x3c,0x66,0x66,0x66,0x3c,0x00},
    ['p' - 32] = {0x00,0x00,0x7c,0x66,0x66,0x7c,0x60,0x60},
    ['q' - 32] = {0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x06},
    ['r' - 32] = {0x00,0x00,0x7c,0x66,0x60,0x60,0x60,0x00},
    ['s' - 32] = {0x00,0x00,0x3e,0x60,0x3c,0x06,0x7c,0x00},
    ['t' - 32] = {0x18,0x18,0x7e,0x18,0x18,0x18,0x0c,0x00},
    ['u' - 32] = {0x00,0x00,0x66,0x66,0x66,0x66,0x3e,0x00},
    ['v' - 32] = {0x00,0x00,0x66,0x66,0x66,0x3c,0x18,0x00},
    ['w' - 32] = {0x00,0x00,0x63,0x6b,0x7f,0x3e,0x36,0x00},
    ['x' - 32] = {0x00,0x00,0x66,0x3c,0x18,0x3c,0x66,0x00},
    ['y' - 32] = {0x00,0x00,0x66,0x66,0x66,0x3e,0x06,0x3c},
    ['z' - 32] = {0x00,0x00,0x7e,0x0c,0x18,0x30,0x7e,0x00},
    ['{' - 32] = {0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0x00},
    ['|' - 32] = {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    ['}' - 32] = {0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0x00},
    ['~' - 32] = {0x76,0xdc,0x00,0x00,0x00,0x00,0x00,0x00},
};

static void draw_text(uint32_t *data, int width, int height, int start_x, int start_y,
                      const char *str, uint32_t color) {
    int cx = start_x;
    int cy = start_y;
    for (size_t i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c == '\n') {
            cx = start_x;
            cy += 10;
            continue;
        }
        if (c < 32 || c > 126) c = '?';
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
        cx += 8;
    }
}

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;

    // Toplevel
    struct wl_surface *top_surface;
    struct xdg_surface *top_xdg_surface;
    struct xdg_toplevel *top_xdg_toplevel;
    int top_width;
    int top_height;
    bool top_configured;

    // Popup Tooltip
    struct wl_surface *popup_surface;
    struct xdg_surface *popup_xdg_surface;
    struct xdg_popup *popup_xdg_popup;
    int popup_width;
    int popup_height;
    bool popup_configured;

    bool running;
};

static int create_shm_file(off_t size) {
    int fd = memfd_create("sparrow-repro-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        perror("memfd_create failed");
        return -1;
    }
    if (ftruncate(fd, size) < 0) {
        perror("ftruncate failed");
        close(fd);
        return -1;
    }
    return fd;
}

static struct wl_buffer *create_tooltip_buffer(struct wl_shm *shm, int width, int height,
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

    // 1. Fill background with the specified fill_color (Alpha=0x00 by default) and subtle border
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (x == 0 || x == width - 1 || y == 0 || y == height - 1) {
                data[y * width + x] = border_color;
            } else {
                data[y * width + x] = fill_color;
            }
        }
    }

    // 2. Draw text over the tooltip background (white and light text, matching Chromium)
    // Wayfire honors wl_surface_set_opaque_region: solid dark #202428 background is rendered,
    // so white text is crisp, high-contrast, and clearly visible.
    // Sparrow ignores opaque_region on external textures: background is transparent, so white
    // text floats over the white page background underneath -> QUASI INVISIBLE!
    draw_text(data, width, height, 10, 8, "Chromium ARGB Tooltip (Making wl_shm fast)", 0xFFF5F5F5);
    draw_text(data, width, height, 10, 22, "https://zamundaa.github.io/wayland/making-wl-shm-fast.html", 0xFFD0D0D0);
    draw_text(data, width, height, 10, 36, "Wayfire: Solid dark card | Sparrow: Invisible BG over white", 0xFF99BBEE);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);

    return buffer;
}

static struct wl_buffer *create_toplevel_buffer(struct wl_shm *shm, int width, int height) {
    int stride = width * 4;
    size_t size = stride * height;

    int fd = create_shm_file(size);
    if (fd < 0) return NULL;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    // Realistic browser window with light grey chrome and pure white page area
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (y < 44) {
                data[y * width + x] = 0xFFE5E7EB; // Light grey browser toolbar
            } else if (y == 44) {
                data[y * width + x] = 0xFFCBD5E1; // Border line
            } else {
                data[y * width + x] = 0xFFFFFFFF; // Pure white webpage area
            }
        }
    }

    // Browser tab
    for (int y = 8; y < 44; y++) {
        for (int x = 16; x < 150; x++) {
            data[y * width + x] = 0xFFFFFFFF;
        }
    }
    draw_text(data, width, height, 26, 20, "Chromium Tab", 0xFF1F2937);

    // Browser URL address bar
    for (int y = 8; y < 36; y++) {
        for (int x = 160; x < width - 20; x++) {
            if (x == 160 || x == width - 21 || y == 8 || y == 35) {
                data[y * width + x] = 0xFFD1D5DB;
            } else {
                data[y * width + x] = 0xFFFFFFFF;
            }
        }
    }
    draw_text(data, width, height, 172, 16, "https://zamundaa.github.io/wayland/making-wl-shm-fast.html", 0xFF4B5563);

    // Webpage content
    draw_text(data, width, height, 40, 68, "Wayland Compositing & wl_shm Buffers (Chromium)", 0xFF111827);
    draw_text(data, width, height, 40, 88, "Article demonstrates opaque_region handling on ARGB tooltips", 0xFF6B7280);

    // Link trigger card
    for (int y = 114; y < 146; y++) {
        for (int x = 40; x < 480; x++) {
            if (x == 40 || x == 479 || y == 114 || y == 145) {
                data[y * width + x] = 0xFFCBD5E1;
            } else {
                data[y * width + x] = 0xFFF1F5F9;
            }
        }
    }
    draw_text(data, width, height, 52, 124, "Hover target: [Link to Xaver's blog: making-wl-shm-fast]", 0xFF2563EB);

    // NOTE: Directly beneath this trigger (y=150 to 220, where the tooltip pops up), the webpage is pure #FFFFFF!

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);

    return buffer;
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void top_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct app_state *app = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    app->top_configured = true;
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

static void popup_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct app_state *app = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    app->popup_configured = true;
}

static const struct xdg_surface_listener popup_xdg_surface_listener = {
    .configure = popup_xdg_surface_configure,
};

static void popup_xdg_popup_configure(void *data, struct xdg_popup *popup,
                                      int32_t x, int32_t y, int32_t width, int32_t height) {
    (void)data; (void)popup; (void)x; (void)y; (void)width; (void)height;
}

static void popup_xdg_popup_done(void *data, struct xdg_popup *popup) {
    (void)popup;
    struct app_state *app = data;
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
}

static const struct xdg_popup_listener popup_xdg_popup_listener = {
    .configure = popup_xdg_popup_configure,
    .popup_done = popup_xdg_popup_done,
    .repositioned = NULL,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version) {
    struct app_state *app = data;
    (void)version;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
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
    // Default alpha = 0x00 with opaque region set (Chromium Linux tooltip case:
    // visible on Wayfire due to opaque region, invisible background on Sparrow)
    uint8_t alpha = 0x00;
    bool set_opaque = true;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--duration=", 11) == 0) {
            duration_sec = atoi(argv[i] + 11);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration_sec = atoi(argv[++i]);
        } else if (strncmp(argv[i], "--alpha=", 8) == 0) {
            alpha = (uint8_t)atoi(argv[i] + 8);
        } else if (strcmp(argv[i], "--alpha") == 0 && i + 1 < argc) {
            alpha = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-opaque") == 0) {
            set_opaque = false;
        }
    }

    struct app_state app = {
        .top_width = 720,
        .top_height = 480,
        .popup_width = 440,
        .popup_height = 52,
        .running = true,
    };

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "[FAIL] Cannot connect to Wayland display\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.shm || !app.wm_base) {
        fprintf(stderr, "[FAIL] Missing required Wayland interfaces\n");
        return 1;
    }

    // 1. Create toplevel window
    app.top_surface = wl_compositor_create_surface(app.compositor);
    app.top_xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.top_surface);
    xdg_surface_add_listener(app.top_xdg_surface, &top_xdg_surface_listener, &app);

    app.top_xdg_toplevel = xdg_surface_get_toplevel(app.top_xdg_surface);
    xdg_toplevel_add_listener(app.top_xdg_toplevel, &top_xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.top_xdg_toplevel, "Sparrow Repro - Tooltip ARGB");
    xdg_toplevel_set_app_id(app.top_xdg_toplevel, "sparrow.repro.tooltip");

    wl_surface_commit(app.top_surface);
    wl_display_roundtrip(app.display);

    struct wl_buffer *top_buf = create_toplevel_buffer(app.shm, app.top_width, app.top_height);
    wl_surface_attach(app.top_surface, top_buf, 0, 0);
    wl_surface_damage_buffer(app.top_surface, 0, 0, app.top_width, app.top_height);
    wl_surface_commit(app.top_surface);
    wl_display_roundtrip(app.display);

    // 2. Create positioner for tooltip popup (placed over the pure white page area)
    struct xdg_positioner *pos = xdg_wm_base_create_positioner(app.wm_base);
    xdg_positioner_set_size(pos, app.popup_width, app.popup_height);
    xdg_positioner_set_anchor_rect(pos, 40, 150, 1, 1);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_constraint_adjustment(pos, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                                                  XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);

    // 3. Create popup surface
    app.popup_surface = wl_compositor_create_surface(app.compositor);
    app.popup_xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.popup_surface);
    xdg_surface_add_listener(app.popup_xdg_surface, &popup_xdg_surface_listener, &app);

    app.popup_xdg_popup = xdg_surface_get_popup(app.popup_xdg_surface, app.top_xdg_surface, pos);
    xdg_popup_add_listener(app.popup_xdg_popup, &popup_xdg_popup_listener, &app);
    xdg_positioner_destroy(pos);

    wl_surface_commit(app.popup_surface);
    wl_display_roundtrip(app.display);

    // 4. Fill background color: RGB=0x20,0x24,0x28 and border RGB=0x3C,0x42,0x4E with requested alpha
    // Default alpha=0x00 makes the card completely transparent in Sparrow (white text on white page),
    // but Wayfire honors wl_surface_set_opaque_region, turning the dark card completely solid & visible!
    uint32_t fill_color = ((uint32_t)alpha << 24) | 0x00202428;
    uint32_t border_color = ((uint32_t)alpha << 24) | 0x003C424E;
    struct wl_buffer *popup_buf = create_tooltip_buffer(app.shm, app.popup_width, app.popup_height,
                                                        fill_color, border_color);

    wl_surface_attach(app.popup_surface, popup_buf, 0, 0);
    wl_surface_damage_buffer(app.popup_surface, 0, 0, app.popup_width, app.popup_height);

    // 5. If requested, set opaque region covering the entire popup (Wayfire honors this, Sparrow ignores it)
    if (set_opaque) {
        struct wl_region *opaque = wl_compositor_create_region(app.compositor);
        wl_region_add(opaque, 0, 0, app.popup_width, app.popup_height);
        wl_surface_set_opaque_region(app.popup_surface, opaque);
        wl_region_destroy(opaque);
    }

    wl_surface_commit(app.popup_surface);
    wl_display_roundtrip(app.display);

    printf("[REPRO-TOOLTIP] Real-case Tooltip ARGB active: size=%dx%d alpha=0x%02X opaque_region=%d\n",
           app.popup_width, app.popup_height, alpha, set_opaque);
    printf("[REPRO-TOOLTIP] -> On Wayfire: Background is solid & visible (opaque region honored)\n");
    printf("[REPRO-TOOLTIP] -> On Sparrow: Background is transparent/invisible (Flutter samples alpha=0x00)\n");
    fflush(stdout);

    struct pollfd pfd = {
        .fd = wl_display_get_fd(app.display),
        .events = POLLIN,
    };

    time_t start = time(NULL);
    while (app.running && (duration_sec <= 0 || (time(NULL) - start < duration_sec))) {
        while (wl_display_prepare_read(app.display) != 0) {
            wl_display_dispatch_pending(app.display);
        }
        wl_display_flush(app.display);

        if (duration_sec > 0 && (time(NULL) - start >= duration_sec)) {
            wl_display_cancel_read(app.display);
            break;
        }

        int ret = poll(&pfd, 1, 100);
        if (ret > 0) {
            wl_display_read_events(app.display);
            wl_display_dispatch_pending(app.display);
        } else if (ret == 0) {
            wl_display_cancel_read(app.display);
        } else {
            wl_display_cancel_read(app.display);
            break;
        }
    }

    printf("[REPRO-TOOLTIP] Finished execution successfully\n");

    if (app.popup_xdg_popup) xdg_popup_destroy(app.popup_xdg_popup);
    if (app.popup_xdg_surface) xdg_surface_destroy(app.popup_xdg_surface);
    if (app.popup_surface) wl_surface_destroy(app.popup_surface);
    if (popup_buf) wl_buffer_destroy(popup_buf);

    xdg_toplevel_destroy(app.top_xdg_toplevel);
    xdg_surface_destroy(app.top_xdg_surface);
    wl_surface_destroy(app.top_surface);
    if (top_buf) wl_buffer_destroy(top_buf);

    xdg_wm_base_destroy(app.wm_base);
    wl_shm_destroy(app.shm);
    wl_compositor_destroy(app.compositor);
    wl_registry_destroy(app.registry);
    wl_display_disconnect(app.display);

    return 0;
}
