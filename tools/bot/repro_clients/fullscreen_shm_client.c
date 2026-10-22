#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

struct buffer_info {
    struct wl_buffer *buffer;
    uint32_t *pixels;
    size_t size;
    int fd;
    bool busy;
};

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    int width;
    int height;
    bool configured;
    bool running;
    FILE *log_fp;

    int frame_count;
    struct buffer_info buf1; // Background frame
    struct buffer_info buf2; // Foreground game/cards frame
};

static int create_shm_file(size_t size) {
    int fd = memfd_create("sparrow-fs-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd >= 0) {
        if (ftruncate(fd, size) < 0) {
            close(fd);
            return -1;
        }
        fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
        return fd;
    }
    return -1;
}

static void buffer_release(void *data, struct wl_buffer *buffer) {
    (void)buffer;
    struct buffer_info *b = data;
    b->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static bool init_buffer(struct app_state *app, struct buffer_info *b, int width, int height) {
    int stride = width * 4;
    b->size = stride * height;
    b->fd = create_shm_file(b->size);
    if (b->fd < 0) return false;

    b->pixels = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, b->fd, 0);
    if (b->pixels == MAP_FAILED) {
        close(b->fd);
        return false;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, b->fd, b->size);
    b->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    wl_buffer_add_listener(b->buffer, &buffer_listener, b);
    b->busy = false;
    return true;
}

static void destroy_buffer(struct buffer_info *b) {
    if (b->buffer) {
        wl_buffer_destroy(b->buffer);
        b->buffer = NULL;
    }
    if (b->pixels && b->pixels != MAP_FAILED) {
        munmap(b->pixels, b->size);
        b->pixels = NULL;
    }
    if (b->fd >= 0) {
        close(b->fd);
        b->fd = -1;
    }
}

// Frame 1: Application background (solid dark green, like kpat table)
static void draw_background_frame(struct buffer_info *b, int width, int height) {
    const uint32_t bg_color = 0xFF1B3B22; // Dark Green Felt
    for (int i = 0; i < width * height; i++) {
        b->pixels[i] = bg_color;
    }
}

// Frame 2: Active game content (simulates cards, menus, UI drawn on top of background)
static void draw_game_content_frame(struct buffer_info *b, int width, int height) {
    const uint32_t bg_color = 0xFF1B3B22;
    for (int i = 0; i < width * height; i++) {
        b->pixels[i] = bg_color;
    }

    // Top menu bar (Dark grey)
    int menu_h = 40;
    for (int y = 0; y < menu_h && y < height; y++) {
        for (int x = 0; x < width; x++) {
            b->pixels[y * width + x] = 0xFF2A2A2A;
        }
    }

    // Draw 7 simulated "cards" like Solitaire
    int card_w = width / 10;
    int card_h = card_w * 3 / 2;
    if (card_w < 50) card_w = 50;
    if (card_h < 75) card_h = 75;

    for (int col = 0; col < 7; col++) {
        int cx = (col + 1) * (width / 9);
        int cy = menu_h + 80;

        for (int y = cy; y < cy + card_h && y < height; y++) {
            for (int x = cx; x < cx + card_w && x < width; x++) {
                // Card border
                if (x == cx || x == cx + card_w - 1 || y == cy || y == cy + card_h - 1) {
                    b->pixels[y * width + x] = 0xFF000000;
                } else if ((col % 2) == 0) {
                    // White card with red center
                    b->pixels[y * width + x] = ((x - cx > 10 && x - cx < card_w - 10) &&
                                                (y - cy > 10 && y - cy < card_h - 10)) ?
                                                0xFFFF0000 : 0xFFFFFFFF;
                } else {
                    // White card with black/blue center
                    b->pixels[y * width + x] = ((x - cx > 10 && x - cx < card_w - 10) &&
                                                (y - cy > 10 && y - cy < card_h - 10)) ?
                                                0xFF0033CC : 0xFFFFFFFF;
                }
            }
        }
    }

    // High-visibility center banner: Gold box with White interior
    int banner_w = width / 2;
    int banner_h = 60;
    int bx = (width - banner_w) / 2;
    int by = height / 2 + 100;
    for (int y = by; y < by + banner_h && y < height; y++) {
        for (int x = bx; x < bx + banner_w && x < width; x++) {
            if (x - bx < 4 || bx + banner_w - x <= 4 || y - by < 4 || by + banner_h - y <= 4) {
                b->pixels[y * width + x] = 0xFFFFD700; // Gold border
            } else {
                b->pixels[y * width + x] = 0xFFFFFFFF; // Pure White fill
            }
        }
    }
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct app_state *app = data;
    xdg_surface_ack_configure(xdg_surface, serial);

    if (!app->configured) {
        app->configured = true;

        init_buffer(app, &app->buf1, app->width, app->height);
        init_buffer(app, &app->buf2, app->width, app->height);

        // Frame 1: Initial background
        draw_background_frame(&app->buf1, app->width, app->height);
        wl_surface_attach(app->surface, app->buf1.buffer, 0, 0);
        wl_surface_damage_buffer(app->surface, 0, 0, app->width, app->height);
        wl_surface_commit(app->surface);
        app->frame_count = 1;

        fprintf(app->log_fp, "[FS_FRAME1_COMMITTED] Initial background committed (%dx%d)\n",
                app->width, app->height);
        fflush(app->log_fp);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height, struct wl_array *states) {
    (void)toplevel; (void)states;
    struct app_state *app = data;
    if (width > 0 && height > 0) {
        app->width = width;
        app->height = height;
    }
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)toplevel;
    struct app_state *app = data;
    app->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
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
    int duration_sec = 6;
    int update_delay_ms = 400;
    const char *log_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--duration=", 11) == 0) {
            duration_sec = atoi(argv[i] + 11);
        } else if (strncmp(argv[i], "--update-delay-ms=", 18) == 0) {
            update_delay_ms = atoi(argv[i] + 18);
        } else if (strncmp(argv[i], "--log=", 6) == 0) {
            log_path = argv[i] + 6;
        }
    }

    struct app_state app = {
        .width = 1920,
        .height = 1200,
        .log_fp = stdout,
        .running = true,
        .configured = false,
        .frame_count = 0,
    };

    if (log_path) {
        app.log_fp = fopen(log_path, "w");
        if (!app.log_fp) {
            app.log_fp = stdout;
        }
    }

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

    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);

    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.xdg_toplevel, "Fullscreen SHM Test");
    xdg_toplevel_set_app_id(app.xdg_toplevel, "sparrow.test.fullscreen_shm");

    // Request fullscreen immediately at startup
    xdg_toplevel_set_fullscreen(app.xdg_toplevel, NULL);

    wl_surface_commit(app.surface);
    wl_display_roundtrip(app.display);

    fprintf(app.log_fp, "[FS_READY] Fullscreen window requested and initial configure acked\n");
    fflush(app.log_fp);

    struct timeval start_tv;
    gettimeofday(&start_tv, NULL);
    bool frame2_done = false;

    while (app.running) {
        wl_display_dispatch_pending(app.display);

        struct timeval now_tv;
        gettimeofday(&now_tv, NULL);
        long elapsed_ms = (now_tv.tv_sec - start_tv.tv_sec) * 1000 +
                          (now_tv.tv_usec - start_tv.tv_usec) / 1000;

        if (!frame2_done && elapsed_ms >= update_delay_ms && app.configured) {
            // Frame 2: Draw the cards/game content and commit with damage
            draw_game_content_frame(&app.buf2, app.width, app.height);
            wl_surface_attach(app.surface, app.buf2.buffer, 0, 0);
            wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
            wl_surface_commit(app.surface);
            wl_display_flush(app.display);

            app.frame_count = 2;
            frame2_done = true;

            fprintf(app.log_fp, "[FS_FRAME2_COMMITTED] Game cards frame committed (%dx%d) after %ldms\n",
                    app.width, app.height, elapsed_ms);
            fflush(app.log_fp);
        } else if (frame2_done && app.configured) {
            // Continuously commit frames like active game loop
            wl_surface_attach(app.surface, app.buf2.buffer, 0, 0);
            wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
            wl_surface_commit(app.surface);
            wl_display_flush(app.display);
        }

        if (duration_sec > 0 && elapsed_ms >= duration_sec * 1000) {
            break;
        }

        usleep(16000); // ~60 Hz loop
    }

    fprintf(app.log_fp, "[FS_FINISH] Completed successfully\n");
    fflush(app.log_fp);

    destroy_buffer(&app.buf1);
    destroy_buffer(&app.buf2);

    xdg_toplevel_destroy(app.xdg_toplevel);
    xdg_surface_destroy(app.xdg_surface);
    wl_surface_destroy(app.surface);

    xdg_wm_base_destroy(app.wm_base);
    wl_shm_destroy(app.shm);
    wl_compositor_destroy(app.compositor);
    wl_registry_destroy(app.registry);
    wl_display_disconnect(app.display);

    if (app.log_fp != stdout) {
        fclose(app.log_fp);
    }

    return 0;
}
