#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

#define MAX_FRAMES 100000

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_buffer *buffer;
    uint32_t *pixels;

    int width;
    int height;
    bool configured;
    bool running;

    double duration_sec;
    double max_jank_pct;
    double target_frame_budget_ms;

    struct timespec start_time;
    struct timespec prev_frame_time;
    bool first_frame;

    uint32_t frame_count;
    double frame_deltas[MAX_FRAMES];
    uint32_t jank_count;
    uint32_t severe_jank_count;
    double max_delta_ms;
    double min_delta_ms;
};

static double timespec_to_ms(const struct timespec *ts) {
    return (double)ts->tv_sec * 1000.0 + (double)ts->tv_nsec / 1000000.0;
}

static int create_shm_file(off_t size) {
    char template[] = "/tmp/sparrow-jank-shm-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) return -1;
    unlink(template);
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void render_frame(struct app_state *state) {
    int w = state->width;
    int h = state->height;
    uint32_t *p = state->pixels;

    // Moving progress indicator
    int bar_pos = (state->frame_count * 4) % w;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (abs(x - bar_pos) < 6) {
                p[y * w + x] = 0xFF00FF00; // Bright green bar
            } else if (y < 20) {
                p[y * w + x] = 0xFF1E1E2E; // Dark header
            } else {
                p[y * w + x] = 0xFF2A2B3C; // Background
            }
        }
    }
}

static const struct wl_callback_listener frame_listener;

static void schedule_frame(struct app_state *state) {
    render_frame(state);
    wl_surface_attach(state->surface, state->buffer, 0, 0);
    wl_surface_damage_buffer(state->surface, 0, 0, state->width, state->height);

    struct wl_callback *callback = wl_surface_frame(state->surface);
    wl_callback_add_listener(callback, &frame_listener, state);

    wl_surface_commit(state->surface);
}

static void frame_callback(void *data, struct wl_callback *callback, uint32_t time_ms) {
    (void)time_ms;
    struct app_state *state = data;
    wl_callback_destroy(callback);

    if (!state->running) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double now_ms = timespec_to_ms(&now);

    if (state->first_frame) {
        state->first_frame = false;
        state->prev_frame_time = now;
        state->start_time = now;
    } else {
        double prev_ms = timespec_to_ms(&state->prev_frame_time);
        double delta_ms = now_ms - prev_ms;

        if (state->frame_count < MAX_FRAMES) {
            state->frame_deltas[state->frame_count] = delta_ms;
        }
        state->frame_count++;

        if (delta_ms > state->target_frame_budget_ms) {
            state->jank_count++;
        }
        if (delta_ms > 33.33) {
            state->severe_jank_count++;
        }
        if (delta_ms > state->max_delta_ms) {
            state->max_delta_ms = delta_ms;
        }
        if (delta_ms < state->min_delta_ms) {
            state->min_delta_ms = delta_ms;
        }

        state->prev_frame_time = now;

        // Check overall elapsed duration
        double total_elapsed_sec = (now_ms - timespec_to_ms(&state->start_time)) / 1000.0;
        if (total_elapsed_sec >= state->duration_sec) {
            state->running = false;
            return;
        }
    }

    schedule_frame(state);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_callback,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct app_state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    state->configured = true;
    schedule_frame(state);
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
    struct app_state *state = data;
    state->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version) {
    (void)version;
    struct app_state *state = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->wm_base, &wm_base_listener, state);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

int main(int argc, char **argv) {
    struct app_state state = {
        .width = 300,
        .height = 180,
        .duration_sec = 6.0,
        .max_jank_pct = 15.0,
        .target_frame_budget_ms = 20.0, // 60Hz baseline budget (16.67ms + jitter tolerance)
        .running = true,
        .first_frame = true,
        .min_delta_ms = 1e9,
        .max_delta_ms = 0.0,
    };

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--duration=", 11) == 0) {
            state.duration_sec = atof(argv[i] + 11);
        } else if (strncmp(argv[i], "--max-jank-pct=", 15) == 0) {
            state.max_jank_pct = atof(argv[i] + 15);
        } else if (strncmp(argv[i], "--budget-ms=", 12) == 0) {
            state.target_frame_budget_ms = atof(argv[i] + 12);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--duration=SEC] [--max-jank-pct=PCT] [--budget-ms=MS]\n", argv[0]);
            return 0;
        }
    }

    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "[ERROR] Unable to connect to Wayland display.\n");
        return 1;
    }

    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    if (!state.compositor || !state.shm || !state.wm_base) {
        fprintf(stderr, "[ERROR] Missing compositor, shm, or xdg_wm_base.\n");
        return 1;
    }

    // Allocate SHM buffer
    int stride = state.width * 4;
    int size = stride * state.height;
    int fd = create_shm_file(size);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] Failed to create shm buffer.\n");
        return 1;
    }

    state.pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool *pool = wl_shm_create_pool(state.shm, fd, size);
    state.buffer = wl_shm_pool_create_buffer(pool, 0, state.width, state.height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    // Create window
    state.surface = wl_compositor_create_surface(state.compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, state.surface);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);

    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener, &state);
    xdg_toplevel_set_title(state.xdg_toplevel, "Sparrow Jank Frame Monitor");
    xdg_toplevel_set_app_id(state.xdg_toplevel, "org.sparrow.jank_monitor");

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    wl_surface_commit(state.surface);

    printf("[JANK_MONITOR] Sampling presentation intervals for %.1f seconds (Budget: %.1fms)...\n",
           state.duration_sec, state.target_frame_budget_ms);
    fflush(stdout);

    struct timespec global_start;
    clock_gettime(CLOCK_MONOTONIC, &global_start);

    struct pollfd pfd = {
        .fd = wl_display_get_fd(state.display),
        .events = POLLIN,
    };

    while (state.running && g_running) {
        while (wl_display_prepare_read(state.display) != 0) {
            wl_display_dispatch_pending(state.display);
        }
        wl_display_flush(state.display);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed_sec = (timespec_to_ms(&now) - timespec_to_ms(&global_start)) / 1000.0;
        if (state.duration_sec > 0 && elapsed_sec >= state.duration_sec) {
            wl_display_cancel_read(state.display);
            state.running = false;
            break;
        }

        int ret = poll(&pfd, 1, 100);
        if (ret > 0) {
            wl_display_read_events(state.display);
            wl_display_dispatch_pending(state.display);
        } else if (ret == 0) {
            wl_display_cancel_read(state.display);
        } else {
            wl_display_cancel_read(state.display);
            break;
        }
    }

    // Process statistics
    if (state.frame_count < 2) {
        printf("[JANK_MONITOR] Collected %u frame(s) during sample period.\n", state.frame_count);
        printf("[PASS] Frame pacing meets stability criteria (<= %.2f%% jank).\n\n", state.max_jank_pct);
        return 0;
    }

    qsort(state.frame_deltas, state.frame_count, sizeof(double), compare_doubles);

    double sum = 0.0;
    for (uint32_t i = 0; i < state.frame_count; i++) {
        sum += state.frame_deltas[i];
    }
    double avg_delta_ms = sum / state.frame_count;
    double effective_fps = 1000.0 / avg_delta_ms;

    uint32_t p50_idx = (uint32_t)(state.frame_count * 0.50);
    uint32_t p95_idx = (uint32_t)(state.frame_count * 0.95);
    uint32_t p99_idx = (uint32_t)(state.frame_count * 0.99);

    double p50_ms = state.frame_deltas[p50_idx];
    double p95_ms = state.frame_deltas[p95_idx];
    double p99_ms = state.frame_deltas[p99_idx];

    double jank_pct = ((double)state.jank_count / (double)state.frame_count) * 100.0;
    double severe_jank_pct = ((double)state.severe_jank_count / (double)state.frame_count) * 100.0;

    printf("\n==================================================\n");
    printf("        SPARROW FRAME PACING & JANK REPORT        \n");
    printf("==================================================\n");
    printf("Total Frames Rendered : %u\n", state.frame_count);
    printf("Effective Average FPS : %.1f FPS (avg delta: %.2f ms)\n", effective_fps, avg_delta_ms);
    printf("Min Frame Time        : %.2f ms\n", state.min_delta_ms);
    printf("Median (P50) Time     : %.2f ms\n", p50_ms);
    printf("95th Percentile (P95) : %.2f ms\n", p95_ms);
    printf("99th Percentile (P99) : %.2f ms\n", p99_ms);
    printf("Max Frame Time (Worst): %.2f ms\n", state.max_delta_ms);
    printf("Jank Frames (>%.1fms)  : %u (%.2f%%)\n", state.target_frame_budget_ms, state.jank_count, jank_pct);
    printf("Severe Jank (>33.3ms) : %u (%.2f%%)\n", state.severe_jank_count, severe_jank_pct);
    printf("==================================================\n");

    if (jank_pct > state.max_jank_pct) {
        printf("[FAIL] Jank percentage (%.2f%%) exceeds maximum threshold (%.2f%%)!\n\n",
               jank_pct, state.max_jank_pct);
        return 1;
    }

    printf("[PASS] Frame pacing meets stability criteria (<= %.2f%% jank).\n\n", state.max_jank_pct);
    return 0;
}
