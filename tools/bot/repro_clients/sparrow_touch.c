#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

struct touch_app
{
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct wl_output *output;
    struct zwlr_virtual_pointer_manager_v1 *vp_mgr;
    struct zwlr_virtual_pointer_v1 *vp;
    uint32_t screen_w;
    uint32_t screen_h;
};

static uint32_t get_timestamp_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void output_geometry(void *data, struct wl_output *wl_output,
    int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
    int32_t subpixel, const char *make, const char *model, int32_t transform)
{
    (void)data;
    (void)wl_output;
    (void)x;
    (void)y;
    (void)physical_width;
    (void)physical_height;
    (void)subpixel;
    (void)make;
    (void)model;
    (void)transform;
}

static void output_mode(void *data, struct wl_output *wl_output,
    uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
    (void)wl_output;
    (void)refresh;
    struct touch_app *app = data;
    if (flags & WL_OUTPUT_MODE_CURRENT)
    {
        if ((width > 0) && (height > 0))
        {
            app->screen_w = (uint32_t)width;
            app->screen_h = (uint32_t)height;
        }
    }
}

static void output_done(void *data, struct wl_output *wl_output)
{
    (void)data;
    (void)wl_output;
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
    (void)data;
    (void)wl_output;
    (void)factor;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode     = output_mode,
    .done     = output_done,
    .scale    = output_scale,
};

static void registry_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    struct touch_app *app = data;
    (void)version;
    if (strcmp(interface, wl_seat_interface.name) == 0)
    {
        if (!app->seat)
        {
            app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        }
    } else if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0)
    {
        app->vp_mgr = wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, 2);
    } else if (strcmp(interface, wl_output_interface.name) == 0)
    {
        if (!app->output)
        {
            app->output = wl_registry_bind(registry, name, &wl_output_interface, 2);
            wl_output_add_listener(app->output, &output_listener, app);
        }
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void move_to(struct touch_app *app, uint32_t x, uint32_t y)
{
    zwlr_virtual_pointer_v1_motion_absolute(app->vp, get_timestamp_ms(), x, y, app->screen_w, app->screen_h);
    zwlr_virtual_pointer_v1_frame(app->vp);
    wl_display_flush(app->display);
}

static void touch_down(struct touch_app *app)
{
    zwlr_virtual_pointer_v1_button(app->vp, get_timestamp_ms(), BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
    zwlr_virtual_pointer_v1_frame(app->vp);
    wl_display_flush(app->display);
}

static void touch_up(struct touch_app *app)
{
    zwlr_virtual_pointer_v1_button(app->vp, get_timestamp_ms(), BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(app->vp);
    wl_display_flush(app->display);
}

static void do_tap(struct touch_app *app, uint32_t x, uint32_t y, uint32_t hold_ms)
{
    move_to(app, x, y);
    usleep(15000);
    touch_down(app);
    if (hold_ms > 0)
    {
        usleep(hold_ms * 1000);
    } else
    {
        usleep(30000);
    }

    touch_up(app);
    usleep(10000);
}

static void do_swipe(struct touch_app *app, uint32_t x1, uint32_t y1,
    uint32_t x2, uint32_t y2, uint32_t duration_ms, uint32_t steps)
{
    if (steps < 2)
    {
        steps = 2;
    }

    if (duration_ms < 10)
    {
        duration_ms = 10;
    }

    move_to(app, x1, y1);
    usleep(20000); // 20ms settle
    touch_down(app);
    usleep(10000);

    uint32_t step_delay_us = (duration_ms * 1000) / steps;
    for (uint32_t i = 1; i <= steps; i++)
    {
        double t = (double)i / (double)steps;
        // Smooth easing (ease-out cubic)
        double ease    = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
        uint32_t cur_x = (uint32_t)((double)x1 + ((double)x2 - (double)x1) * ease);
        uint32_t cur_y = (uint32_t)((double)y1 + ((double)y2 - (double)y1) * ease);

        move_to(app, cur_x, cur_y);
        usleep(step_delay_us);
    }

    usleep(15000);
    touch_up(app);
    usleep(15000);
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s tap <x> <y> [hold_ms] [screen_w] [screen_h]\n"
        "  %s double-tap <x> <y> [screen_w] [screen_h]\n"
        "  %s long-press <x> <y> [hold_ms] [screen_w] [screen_h]\n"
        "  %s swipe <x1> <y1> <x2> <y2> [duration_ms] [steps] [screen_w] [screen_h]\n"
        "  %s swipe-left [duration_ms]\n"
        "  %s swipe-right [duration_ms]\n"
        "  %s swipe-up [duration_ms]\n"
        "  %s swipe-down [duration_ms]\n"
        "  %s edge-bottom [duration_ms]\n",
        prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

static uint32_t parse_u32(const char *str, uint32_t default_val)
{
    if (!str || !*str)
    {
        return default_val;
    }

    char *endptr = NULL;
    unsigned long val = strtoul(str, &endptr, 10);
    return (endptr != str) ? (uint32_t)val : default_val;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    struct touch_app app = {
        .display  = NULL,
        .registry = NULL,
        .seat     = NULL,
        .output   = NULL,
        .vp_mgr   = NULL,
        .vp = NULL,
        .screen_w = 1280,
        .screen_h = 720,
    };

    app.display = wl_display_connect(NULL);
    if (!app.display)
    {
        fprintf(stderr, "[FAIL] Cannot connect to Wayland display\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display); // Initial bind
    wl_display_roundtrip(app.display); // Receive output modes

    if (!app.vp_mgr)
    {
        fprintf(stderr, "[FAIL] zwlr_virtual_pointer_manager_v1 is not supported by compositor\n");
        wl_display_disconnect(app.display);
        return 1;
    }

    app.vp = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(app.vp_mgr, app.seat);
    if (!app.vp)
    {
        fprintf(stderr, "[FAIL] Could not create virtual pointer device\n");
        wl_display_disconnect(app.display);
        return 1;
    }

    const char *cmd = argv[1];

    if (!strcasecmp(cmd, "tap"))
    {
        if (argc < 4)
        {
            print_usage(argv[0]);
            return 1;
        }

        uint32_t x = parse_u32(argv[2], 0);
        uint32_t y = parse_u32(argv[3], 0);
        uint32_t hold_ms = (argc >= 5) ? parse_u32(argv[4], 30) : 30;
        if (argc >= 7)
        {
            app.screen_w = parse_u32(argv[5], app.screen_w);
            app.screen_h = parse_u32(argv[6], app.screen_h);
        }

        do_tap(&app, x, y, hold_ms);
    } else if (!strcasecmp(cmd, "double-tap"))
    {
        if (argc < 4)
        {
            print_usage(argv[0]);
            return 1;
        }

        uint32_t x = parse_u32(argv[2], 0);
        uint32_t y = parse_u32(argv[3], 0);
        if (argc >= 6)
        {
            app.screen_w = parse_u32(argv[4], app.screen_w);
            app.screen_h = parse_u32(argv[5], app.screen_h);
        }

        do_tap(&app, x, y, 20);
        usleep(120000); // 120ms gap
        do_tap(&app, x, y, 20);
    } else if (!strcasecmp(cmd, "long-press"))
    {
        if (argc < 4)
        {
            print_usage(argv[0]);
            return 1;
        }

        uint32_t x = parse_u32(argv[2], 0);
        uint32_t y = parse_u32(argv[3], 0);
        uint32_t hold_ms = (argc >= 5) ? parse_u32(argv[4], 600) : 600;
        if (argc >= 7)
        {
            app.screen_w = parse_u32(argv[5], app.screen_w);
            app.screen_h = parse_u32(argv[6], app.screen_h);
        }

        do_tap(&app, x, y, hold_ms);
    } else if (!strcasecmp(cmd, "swipe"))
    {
        if (argc < 6)
        {
            print_usage(argv[0]);
            return 1;
        }

        uint32_t x1 = parse_u32(argv[2], 0);
        uint32_t y1 = parse_u32(argv[3], 0);
        uint32_t x2 = parse_u32(argv[4], 0);
        uint32_t y2 = parse_u32(argv[5], 0);
        uint32_t duration_ms = (argc >= 7) ? parse_u32(argv[6], 250) : 250;
        uint32_t steps = (argc >= 8) ? parse_u32(argv[7], 16) : 16;
        if (argc >= 10)
        {
            app.screen_w = parse_u32(argv[8], app.screen_w);
            app.screen_h = parse_u32(argv[9], app.screen_h);
        }

        do_swipe(&app, x1, y1, x2, y2, duration_ms, steps);
    } else if (!strcasecmp(cmd, "swipe-left"))
    {
        uint32_t duration_ms = (argc >= 3) ? parse_u32(argv[2], 250) : 250;
        uint32_t mid_y   = app.screen_h / 2;
        uint32_t start_x = (uint32_t)(app.screen_w * 0.85);
        uint32_t end_x   = (uint32_t)(app.screen_w * 0.15);
        do_swipe(&app, start_x, mid_y, end_x, mid_y, duration_ms, 18);
    } else if (!strcasecmp(cmd, "swipe-right"))
    {
        uint32_t duration_ms = (argc >= 3) ? parse_u32(argv[2], 250) : 250;
        uint32_t mid_y   = app.screen_h / 2;
        uint32_t start_x = (uint32_t)(app.screen_w * 0.15);
        uint32_t end_x   = (uint32_t)(app.screen_w * 0.85);
        do_swipe(&app, start_x, mid_y, end_x, mid_y, duration_ms, 18);
    } else if (!strcasecmp(cmd, "swipe-up"))
    {
        uint32_t duration_ms = (argc >= 3) ? parse_u32(argv[2], 250) : 250;
        uint32_t mid_x   = app.screen_w / 2;
        uint32_t start_y = (uint32_t)(app.screen_h * 0.85);
        uint32_t end_y   = (uint32_t)(app.screen_h * 0.15);
        do_swipe(&app, mid_x, start_y, mid_x, end_y, duration_ms, 18);
    } else if (!strcasecmp(cmd, "swipe-down"))
    {
        uint32_t duration_ms = (argc >= 3) ? parse_u32(argv[2], 250) : 250;
        uint32_t mid_x   = app.screen_w / 2;
        uint32_t start_y = (uint32_t)(app.screen_h * 0.15);
        uint32_t end_y   = (uint32_t)(app.screen_h * 0.85);
        do_swipe(&app, mid_x, start_y, mid_x, end_y, duration_ms, 18);
    } else if (!strcasecmp(cmd, "edge-bottom"))
    {
        // Edge gesture: swipe up from bottom edge to open drawer
        uint32_t duration_ms = (argc >= 3) ? parse_u32(argv[2], 200) : 200;
        uint32_t mid_x   = app.screen_w / 2;
        uint32_t start_y = app.screen_h - 5;
        uint32_t end_y   = app.screen_h - 180;
        do_swipe(&app, mid_x, start_y, mid_x, end_y, duration_ms, 15);
    } else
    {
        fprintf(stderr, "Unknown command '%s'\n", cmd);
        print_usage(argv[0]);
        zwlr_virtual_pointer_v1_destroy(app.vp);
        wl_display_disconnect(app.display);
        return 1;
    }

    zwlr_virtual_pointer_v1_destroy(app.vp);
    wl_display_roundtrip(app.display);
    wl_display_disconnect(app.display);

    return 0;
}
