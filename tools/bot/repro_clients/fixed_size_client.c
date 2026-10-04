#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_touch *touch;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    int width;
    int height;
    double last_pointer_x;
    double last_pointer_y;
    bool pointer_inside;

    FILE *log_fp;
    bool running;
};

static int create_shm_file(off_t size) {
    int fd = memfd_create("sparrow-fixed-size-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static struct wl_buffer *create_pattern_buffer(struct wl_shm *shm, int width, int height) {
    int stride = width * 4;
    size_t size = stride * height;
    int fd = create_shm_file(size);
    if (fd < 0) return NULL;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    // Background: Dark Slate #1E232A
    const uint32_t bg_color = 0xFF1E232A;
    // Border: Vibrant Cyan #00E5FF
    const uint32_t border_color = 0xFF00E5FF;
    // Center: Yellow #FFEA00
    const uint32_t center_color = 0xFFFFEA00;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // 4px border
            if (x < 4 || x >= width - 4 || y < 4 || y >= height - 4) {
                data[y * width + x] = border_color;
            } else if (abs(x - width / 2) <= 1 || abs(y - height / 2) <= 1) {
                // Center crosshair
                data[y * width + x] = center_color;
            } else if (abs(x - width / 2) <= 20 && abs(y - height / 2) <= 20 &&
                       (abs(x - width / 2) == 20 || abs(y - height / 2) == 20)) {
                // Center target box
                data[y * width + x] = center_color;
            } else if (x < 40 && y < 40) {
                // Top-left corner box (Red)
                data[y * width + x] = 0xFFFF3366;
            } else if (x >= width - 40 && y < 40) {
                // Top-right corner box (Green)
                data[y * width + x] = 0xFF33FF66;
            } else if (x < 40 && y >= height - 40) {
                // Bottom-left corner box (Blue)
                data[y * width + x] = 0xFF3399FF;
            } else if (x >= width - 40 && y >= height - 40) {
                // Bottom-right corner box (Purple)
                data[y * width + x] = 0xFFCC33FF;
            } else {
                data[y * width + x] = bg_color;
            }
        }
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);

    return buffer;
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)serial; (void)surface;
    struct app_state *app = data;
    app->pointer_inside = true;
    app->last_pointer_x = wl_fixed_to_double(sx);
    app->last_pointer_y = wl_fixed_to_double(sy);
    fprintf(app->log_fp, "[FIXED_POINTER] enter x=%.2f y=%.2f\n", app->last_pointer_x, app->last_pointer_y);
    fflush(app->log_fp);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {
    (void)pointer; (void)serial; (void)surface;
    struct app_state *app = data;
    app->pointer_inside = false;
    fprintf(app->log_fp, "[FIXED_POINTER] leave\n");
    fflush(app->log_fp);
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)time;
    struct app_state *app = data;
    app->last_pointer_x = wl_fixed_to_double(sx);
    app->last_pointer_y = wl_fixed_to_double(sy);
    fprintf(app->log_fp, "[FIXED_POINTER] motion x=%.2f y=%.2f\n", app->last_pointer_x, app->last_pointer_y);
    fflush(app->log_fp);
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer; (void)serial; (void)time;
    struct app_state *app = data;
    fprintf(app->log_fp, "[FIXED_POINTER] button=%u state=%u x=%.2f y=%.2f\n",
            button, state, app->last_pointer_x, app->last_pointer_y);
    fflush(app->log_fp);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {
    (void)data; (void)pointer; (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void touch_down(void *data, struct wl_touch *touch, uint32_t serial,
                       uint32_t time, struct wl_surface *surface,
                       int32_t id, wl_fixed_t x, wl_fixed_t y) {
    (void)touch; (void)serial; (void)surface;
    struct app_state *app = data;
    fprintf(app->log_fp, "[FIXED_TOUCH] down time=%u id=%d x=%.2f y=%.2f\n",
            time, id, wl_fixed_to_double(x), wl_fixed_to_double(y));
    fflush(app->log_fp);
}

static void touch_up(void *data, struct wl_touch *touch, uint32_t serial,
                     uint32_t time, int32_t id) {
    (void)touch; (void)serial;
    struct app_state *app = data;
    fprintf(app->log_fp, "[FIXED_TOUCH] up time=%u id=%d\n", time, id);
    fflush(app->log_fp);
}

static void touch_motion(void *data, struct wl_touch *touch, uint32_t time,
                         int32_t id, wl_fixed_t x, wl_fixed_t y) {
    (void)touch;
    struct app_state *app = data;
    fprintf(app->log_fp, "[FIXED_TOUCH] motion time=%u id=%d x=%.2f y=%.2f\n",
            time, id, wl_fixed_to_double(x), wl_fixed_to_double(y));
    fflush(app->log_fp);
}

static void touch_frame(void *data, struct wl_touch *touch) {
    (void)data; (void)touch;
}

static void touch_cancel(void *data, struct wl_touch *touch) {
    (void)touch;
    struct app_state *app = data;
    fprintf(app->log_fp, "[FIXED_TOUCH] cancel\n");
    fflush(app->log_fp);
}

static const struct wl_touch_listener touch_listener = {
    .down = touch_down,
    .up = touch_up,
    .motion = touch_motion,
    .frame = touch_frame,
    .cancel = touch_cancel,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct app_state *app = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && app->pointer) {
        wl_pointer_destroy(app->pointer);
        app->pointer = NULL;
    }

    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !app->touch) {
        app->touch = wl_seat_get_touch(seat);
        wl_touch_add_listener(app->touch, &touch_listener, app);
    } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && app->touch) {
        wl_touch_destroy(app->touch);
        app->touch = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height, struct wl_array *states) {
    (void)data; (void)toplevel; (void)width; (void)height; (void)states;
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
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
        wl_seat_add_listener(app->seat, &seat_listener, app);
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
    const char *log_path = NULL;
    int width = 640;
    int height = 480;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--duration=", 11) == 0) {
            duration_sec = atoi(argv[i] + 11);
        } else if (strncmp(argv[i], "--log=", 6) == 0) {
            log_path = argv[i] + 6;
        } else if (strncmp(argv[i], "--width=", 8) == 0) {
            width = atoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--height=", 9) == 0) {
            height = atoi(argv[i] + 9);
        }
    }

    struct app_state app = {
        .width = width,
        .height = height,
        .log_fp = stdout,
        .running = true,
    };

    if (log_path) {
        app.log_fp = fopen(log_path, "w");
        if (!app.log_fp) {
            perror("Failed to open log file");
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
    xdg_toplevel_set_title(app.xdg_toplevel, "Fixed Size Client");
    xdg_toplevel_set_app_id(app.xdg_toplevel, "sparrow.fixed.client");

    // Fix the window size by setting min_size == max_size
    xdg_toplevel_set_min_size(app.xdg_toplevel, app.width, app.height);
    xdg_toplevel_set_max_size(app.xdg_toplevel, app.width, app.height);

    wl_surface_commit(app.surface);
    wl_display_roundtrip(app.display);

    struct wl_buffer *buf = create_pattern_buffer(app.shm, app.width, app.height);
    wl_surface_attach(app.surface, buf, 0, 0);
    wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
    wl_surface_commit(app.surface);
    wl_display_roundtrip(app.display);

    fprintf(app.log_fp, "[FIXED_READY] Window mapped: size=%dx%d min=%dx%d max=%dx%d\n",
            app.width, app.height, app.width, app.height, app.width, app.height);
    fflush(app.log_fp);

    time_t start = time(NULL);
    while (app.running && (duration_sec <= 0 || (time(NULL) - start < duration_sec))) {
        if (wl_display_dispatch(app.display) < 0) {
            break;
        }
    }

    fprintf(app.log_fp, "[FIXED_FINISH] Session completed\n");
    fflush(app.log_fp);

    if (app.log_fp != stdout) {
        fclose(app.log_fp);
    }

    if (app.pointer) wl_pointer_destroy(app.pointer);
    if (app.touch) wl_touch_destroy(app.touch);
    if (app.seat) wl_seat_destroy(app.seat);

    xdg_toplevel_destroy(app.xdg_toplevel);
    xdg_surface_destroy(app.xdg_surface);
    wl_surface_destroy(app.surface);
    wl_buffer_destroy(buf);

    xdg_wm_base_destroy(app.wm_base);
    wl_shm_destroy(app.shm);
    wl_compositor_destroy(app.compositor);
    wl_registry_destroy(app.registry);
    wl_display_disconnect(app.display);

    return 0;
}
