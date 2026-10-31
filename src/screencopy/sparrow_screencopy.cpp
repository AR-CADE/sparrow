#include <algorithm>
#include <assert.h>
#include <cmath>
#include <drm_fourcc.h>
#include <pixman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cstddef>
#include <sparrow/nonstd/wlroots-full.hpp>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "../core.hpp"
#include "../output.hpp"
#include "../surface/view.hpp"
#include "sparrow_screencopy.hpp"
#include "wlr-screencopy-unstable-v1-protocol.h"

#define SCREENCOPY_MANAGER_VERSION 3

struct sparrow_screencopy_manager_v1
{
    struct wl_global *global;
    struct wl_list frames; // sparrow_screencopy_frame_v1.link
    struct wl_listener display_destroy;
};

struct sparrow_screencopy_client
{
    int ref;
    struct sparrow_screencopy_manager_v1 *manager;
    struct wl_list damages;
};

struct sparrow_screencopy_damage
{
    struct wl_list link;
    struct wlr_output *output;
    struct pixman_region32 damage;
    struct wl_listener output_precommit;
    struct wl_listener output_destroy;
};

struct sparrow_screencopy_frame_v1
{
    struct wl_resource *resource;
    struct sparrow_screencopy_client *client;
    struct wl_list link; // sparrow_screencopy_manager_v1.frames

    uint32_t shm_format, dmabuf_format; // DRM format codes
    struct wlr_box box;
    int shm_stride;

    bool overlay_cursor;
    bool cursor_locked;
    bool with_damage;

    struct wlr_output *output;
    struct wlr_buffer *buffer;
    enum wlr_buffer_cap buffer_cap;

    struct wl_listener output_commit;
    struct wl_listener output_destroy;
};

static void frame_handle_copy(struct wl_client *wl_client,
    struct wl_resource *frame_resource,
    struct wl_resource *buffer_resource);
static void frame_handle_destroy(struct wl_client *wl_client,
    struct wl_resource *frame_resource);
static void frame_handle_copy_with_damage(struct wl_client *wl_client,
    struct wl_resource *frame_resource,
    struct wl_resource *buffer_resource);

static const struct zwlr_screencopy_frame_v1_interface frame_impl = {
    .copy    = frame_handle_copy,
    .destroy = frame_handle_destroy,
    .copy_with_damage = frame_handle_copy_with_damage,
};

static void manager_handle_capture_output(struct wl_client *wl_client,
    struct wl_resource *manager_resource,
    uint32_t id, int32_t overlay_cursor,
    struct wl_resource *output_resource);
static void manager_handle_capture_output_region(
    struct wl_client *wl_client, struct wl_resource *manager_resource,
    uint32_t id, int32_t overlay_cursor, struct wl_resource *output_resource,
    int32_t x, int32_t y, int32_t width, int32_t height);
static void manager_handle_destroy(struct wl_client *wl_client,
    struct wl_resource *manager_resource);

static const struct zwlr_screencopy_manager_v1_interface manager_impl = {
    .capture_output = manager_handle_capture_output,
    .capture_output_region = manager_handle_capture_output_region,
    .destroy = manager_handle_destroy,
};

static int clamp_int_val(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }

    if (v > hi)
    {
        return hi;
    }

    return v;
}

static int compute_ellipse_inset(double dy, int rx, int ry)
{
    if ((rx <= 0) || (ry <= 0))
    {
        return 0;
    }

    const double ady = fabs(dy);
    if (ady >= (double)ry)
    {
        return rx;
    }

    const double t = 1.0 - (ady * ady) / ((double)ry * (double)ry);
    if (t <= 0.0)
    {
        return rx;
    }

    const double x_boundary = (double)rx - (double)rx * sqrt(t);
    int inset = (int)floor(x_boundary);
    return clamp_int_val(inset, 0, rx);
}

static void init_rounded_rect_region(pixman_region32_t *dst, int x, int y,
    int width, int height, int radius)
{
    pixman_region32_init(dst);
    if ((width <= 0) || (height <= 0))
    {
        return;
    }

    const int half_w = width / 2;
    const int half_h = height / 2;
    const int r = clamp_int_val(radius, 0, std::min(half_w, half_h));

    if (r <= 0)
    {
        pixman_region32_union_rect(dst, dst, x, y, width, height);
        return;
    }

    const int middle_h = height - (2 * r);
    if (middle_h > 0)
    {
        pixman_region32_union_rect(dst, dst, x, y + r, width, middle_h);
    }

    for (int row = 0; row < r; row++)
    {
        const double dy = ((double)row + 0.5) - (double)r;
        const int inset = compute_ellipse_inset(dy, r, r);
        const int w     = width - (2 * inset);
        if (w > 0)
        {
            pixman_region32_union_rect(dst, dst, x + inset, y + row, w, 1);
        }
    }

    for (int row = height - r; row < height; row++)
    {
        if (row < 0)
        {
            continue;
        }

        const double dy = ((double)row + 0.5) - (double)(height - r);
        const int inset = compute_ellipse_inset(dy, r, r);
        const int w     = width - (2 * inset);
        if (w > 0)
        {
            pixman_region32_union_rect(dst, dst, x + inset, y + row, w, 1);
        }
    }
}

static uint32_t drm_format_to_bytes_per_pixel(uint32_t fmt)
{
    switch (fmt)
    {
      case DRM_FORMAT_XRGB8888:
      case DRM_FORMAT_ARGB8888:
      case DRM_FORMAT_XBGR8888:
      case DRM_FORMAT_ABGR8888:
      case DRM_FORMAT_RGBX8888:
      case DRM_FORMAT_RGBA8888:
      case DRM_FORMAT_BGRX8888:
      case DRM_FORMAT_BGRA8888:
        return 4;

      case DRM_FORMAT_RGB888:
      case DRM_FORMAT_BGR888:
        return 3;

      case DRM_FORMAT_RGB565:
      case DRM_FORMAT_BGR565:
        return 2;

      default:
        return 4;
    }
}

static enum wl_shm_format convert_drm_format_to_wl_shm(uint32_t fmt)
{
    switch (fmt)
    {
      case DRM_FORMAT_XRGB8888:
        return WL_SHM_FORMAT_XRGB8888;

      case DRM_FORMAT_ARGB8888:
        return WL_SHM_FORMAT_ARGB8888;

      default:
        return static_cast<enum wl_shm_format>(fmt);
    }
}

static struct sparrow_screencopy_damage *screencopy_damage_find(struct sparrow_screencopy_client *client,
    struct wlr_output *output)
{
    struct sparrow_screencopy_damage *damage = nullptr;
    wl_list_for_each(damage, &client->damages, link)
    {
        if (damage->output == output)
        {
            return damage;
        }
    }
    return nullptr;
}

static void screencopy_damage_accumulate(struct sparrow_screencopy_damage *damage,
    const struct wlr_output_state *state)
{
    struct pixman_region32 *region = &damage->damage;
    struct wlr_output *output = damage->output;

    if (state->committed & WLR_OUTPUT_STATE_DAMAGE)
    {
        pixman_region32_union(region, region, &state->damage);
        pixman_region32_intersect_rect(region, region, 0, 0, output->width,
            output->height);
    } else if (state->committed & WLR_OUTPUT_STATE_BUFFER)
    {
        pixman_region32_union_rect(region, region, 0, 0, output->width,
            output->height);
    }
}

static void screencopy_damage_handle_output_precommit(struct wl_listener *listener,
    void *data)
{
    struct sparrow_screencopy_damage *damage =
        wl_container_of(listener, damage, output_precommit);
    const auto *event =
        static_cast<const struct wlr_output_event_precommit*>(data);
    screencopy_damage_accumulate(damage, event->state);
}

static void screencopy_damage_destroy(struct sparrow_screencopy_damage *damage)
{
    wl_list_remove(&damage->output_destroy.link);
    wl_list_remove(&damage->output_precommit.link);
    wl_list_remove(&damage->link);
    pixman_region32_fini(&damage->damage);
    free(damage);
}

static void screencopy_damage_handle_output_destroy(struct wl_listener *listener,
    void *data)
{
    (void)data;
    struct sparrow_screencopy_damage *damage =
        wl_container_of(listener, damage, output_destroy);
    screencopy_damage_destroy(damage);
}

static struct sparrow_screencopy_damage *screencopy_damage_create(struct sparrow_screencopy_client *client,
    struct wlr_output *output)
{
    auto *damage = static_cast<struct sparrow_screencopy_damage*>(
        calloc(1, sizeof(struct sparrow_screencopy_damage)));
    if (!damage)
    {
        return nullptr;
    }

    damage->output = output;
    pixman_region32_init_rect(&damage->damage, 0, 0, output->width,
        output->height);
    wl_list_insert(&client->damages, &damage->link);

    wl_signal_add(&output->events.precommit, &damage->output_precommit);
    damage->output_precommit.notify = screencopy_damage_handle_output_precommit;

    wl_signal_add(&output->events.destroy, &damage->output_destroy);
    damage->output_destroy.notify = screencopy_damage_handle_output_destroy;

    return damage;
}

static struct sparrow_screencopy_damage *screencopy_damage_get_or_create(
    struct sparrow_screencopy_client *client,
    struct wlr_output *output)
{
    struct sparrow_screencopy_damage *damage =
        screencopy_damage_find(client, output);
    return damage ? damage : screencopy_damage_create(client, output);
}

static void client_unref(struct sparrow_screencopy_client *client)
{
    assert(client->ref > 0);
    if (--client->ref != 0)
    {
        return;
    }

    struct sparrow_screencopy_damage *damage, *tmp_damage;
    wl_list_for_each_safe(damage, tmp_damage, &client->damages, link)
    {
        screencopy_damage_destroy(damage);
    }

    free(client);
}

static struct sparrow_screencopy_frame_v1 *frame_from_resource(struct wl_resource *resource)
{
    assert(wl_resource_instance_of(resource, &zwlr_screencopy_frame_v1_interface,
        &frame_impl));
    return static_cast<struct sparrow_screencopy_frame_v1*>(
        wl_resource_get_user_data(resource));
}

static void frame_destroy(struct sparrow_screencopy_frame_v1 *frame)
{
    if (frame == nullptr)
    {
        return;
    }

    if ((frame->output != nullptr) && (frame->buffer != nullptr))
    {
        wlr_output_lock_attach_render(frame->output, false);
        if (frame->cursor_locked)
        {
            wlr_output_lock_software_cursors(frame->output, false);
        }
    }

    wl_list_remove(&frame->link);
    wl_list_remove(&frame->output_commit.link);
    wl_list_remove(&frame->output_destroy.link);
    wl_resource_set_user_data(frame->resource, nullptr);
    if (frame->buffer != nullptr)
    {
        wlr_buffer_unlock(frame->buffer);
    }

    client_unref(frame->client);
    free(frame);
}

static void frame_send_damage(struct sparrow_screencopy_frame_v1 *frame)
{
    if (!frame->with_damage)
    {
        return;
    }

    struct sparrow_screencopy_damage *damage =
        screencopy_damage_get_or_create(frame->client, frame->output);
    if (damage == nullptr)
    {
        return;
    }

    int n_boxes = 0;
    const pixman_box32_t *boxes =
        pixman_region32_rectangles(&damage->damage, &n_boxes);
    for (int i = 0; i < n_boxes; i++)
    {
        const pixman_box32_t *box = &boxes[i];
        int damage_x     = box->x1;
        int damage_y     = box->y1;
        int damage_width = box->x2 - box->x1;
        int damage_height = box->y2 - box->y1;

        zwlr_screencopy_frame_v1_send_damage(frame->resource, damage_x, damage_y,
            damage_width, damage_height);
    }

    pixman_region32_clear(&damage->damage);
}

static void frame_send_ready(struct sparrow_screencopy_frame_v1 *frame,
    struct timespec *when)
{
    time_t tv_sec = when->tv_sec;
    uint32_t tv_sec_hi = (sizeof(tv_sec) > 4) ? (uint32_t)(tv_sec >> 32) : 0;
    uint32_t tv_sec_lo = (uint32_t)(tv_sec & 0xFFFFFFFF);
    zwlr_screencopy_frame_v1_send_ready(frame->resource, tv_sec_hi, tv_sec_lo,
        when->tv_nsec);
}

static uint32_t get_opaque_black_pixel(uint32_t format)
{
    switch (format)
    {
      case DRM_FORMAT_ARGB8888:
      case DRM_FORMAT_XRGB8888:
      case DRM_FORMAT_ABGR8888:
      case DRM_FORMAT_XBGR8888:
        return 0xFF000000;

      case DRM_FORMAT_RGBA8888:
      case DRM_FORMAT_RGBX8888:
      case DRM_FORMAT_BGRA8888:
      case DRM_FORMAT_BGRX8888:
        return 0x000000FF;

      default:
        return 0xFF000000;
    }
}

static void censor_shm_pixels(struct sparrow_screencopy_frame_v1 *frame,
    void *data, uint32_t format, size_t stride)
{
    const auto instance = Core::instance();
    if (!instance || !frame || !frame->output || !data)
    {
        return;
    }

    const uint32_t bpp = drm_format_to_bytes_per_pixel(format);
    const uint32_t black_pixel = get_opaque_black_pixel(format);

    if (instance->is_output_sensitive(frame->output))
    {
        auto *buf_bytes = static_cast<uint8_t*>(data);
        for (int row = 0; row < frame->box.height; ++row)
        {
            uint8_t *row_ptr = buf_bytes + (row * stride);
            if (bpp == 4)
            {
                auto *pixels = reinterpret_cast<uint32_t*>(row_ptr);
                for (int x = 0; x < frame->box.width; ++x)
                {
                    pixels[x] = black_pixel;
                }
            } else
            {
                memset(row_ptr, 0, static_cast<size_t>(frame->box.width) * bpp);
            }
        }

        return;
    }

    SparrowView *view = nullptr;
    wl_list_for_each(view, &instance->views_list, link)
    {
        struct wlr_box view_buf_box = {};
        int corner_radius_px = 0;
        if (!sparrow_get_sensitive_view_buffer_box(
            instance, view, frame->output, &view_buf_box, &corner_radius_px))
        {
            continue;
        }

        struct wlr_box intersect = {};
        if (!wlr_box_intersection(&intersect, &view_buf_box, &frame->box))
        {
            continue;
        }

        int dst_x = intersect.x - frame->box.x;
        int dst_y = intersect.y - frame->box.y;
        int dst_w = intersect.width;
        int dst_h = intersect.height;

        if ((dst_w <= 0) || (dst_h <= 0))
        {
            continue;
        }

        if (dst_x < 0)
        {
            dst_w += dst_x;
            dst_x  = 0;
        }

        if (dst_y < 0)
        {
            dst_h += dst_y;
            dst_y  = 0;
        }

        if (dst_x + dst_w > frame->box.width)
        {
            dst_w = frame->box.width - dst_x;
        }

        if (dst_y + dst_h > frame->box.height)
        {
            dst_h = frame->box.height - dst_y;
        }

        if ((dst_w <= 0) || (dst_h <= 0))
        {
            continue;
        }

        const int max_r = std::min(view_buf_box.width / 2, view_buf_box.height / 2);
        const int r     = clamp_int_val(corner_radius_px, 0, max_r);

        auto *buf_bytes = static_cast<uint8_t*>(data);
        for (int row = dst_y; row < dst_y + dst_h; ++row)
        {
            int row_x0 = dst_x;
            int row_x1 = dst_x + dst_w;

            if (r > 0)
            {
                const int rel_y = (row + frame->box.y) - view_buf_box.y;
                int inset = 0;
                if (rel_y < r)
                {
                    const double dy = ((double)rel_y + 0.5) - (double)r;
                    inset = compute_ellipse_inset(dy, r, r);
                } else if (rel_y >= view_buf_box.height - r)
                {
                    const double dy =
                        ((double)rel_y + 0.5) - (double)(view_buf_box.height - r);
                    inset = compute_ellipse_inset(dy, r, r);
                }

                if (inset > 0)
                {
                    const int min_x = (view_buf_box.x + inset) - frame->box.x;
                    const int max_x =
                        (view_buf_box.x + view_buf_box.width - inset) - frame->box.x;
                    if (row_x0 < min_x)
                    {
                        row_x0 = min_x;
                    }

                    if (row_x1 > max_x)
                    {
                        row_x1 = max_x;
                    }
                }
            }

            const int span_w = row_x1 - row_x0;
            if (span_w <= 0)
            {
                continue;
            }

            uint8_t *row_ptr =
                buf_bytes + (row * stride) + (static_cast<size_t>(row_x0 * bpp));
            if (bpp == 4)
            {
                auto *pixels = reinterpret_cast<uint32_t*>(row_ptr);
                for (int x = 0; x < span_w; ++x)
                {
                    pixels[x] = black_pixel;
                }
            } else
            {
                memset(row_ptr, 0, static_cast<size_t>(span_w) * bpp);
            }
        }
    }
}

static bool frame_shm_copy(struct sparrow_screencopy_frame_v1 *frame,
    struct wlr_buffer *src_buffer)
{
    struct wlr_output *output     = frame->output;
    struct wlr_renderer *renderer = output->renderer;
    assert(renderer);

    void *data = nullptr;
    uint32_t format = 0;
    size_t stride   = 0;
    if (!wlr_buffer_begin_data_ptr_access(frame->buffer,
        WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data,
        &format, &stride))
    {
        return false;
    }

    const auto instance = Core::instance();
    if (instance && instance->is_output_sensitive(output))
    {
        const uint32_t bpp = drm_format_to_bytes_per_pixel(format);
        const uint32_t black_pixel = get_opaque_black_pixel(format);
        auto *buf_bytes = static_cast<uint8_t*>(data);
        for (int row = 0; row < frame->box.height; ++row)
        {
            uint8_t *row_ptr = buf_bytes + (row * stride);
            if (bpp == 4)
            {
                auto *pixels = reinterpret_cast<uint32_t*>(row_ptr);
                for (int x = 0; x < frame->box.width; ++x)
                {
                    pixels[x] = black_pixel;
                }
            } else
            {
                memset(row_ptr, 0, static_cast<size_t>(frame->box.width) * bpp);
            }
        }

        wlr_buffer_end_data_ptr_access(frame->buffer);
        return true;
    }

    bool ok = false;
    struct wlr_texture *texture = wlr_texture_from_buffer(renderer, src_buffer);
    if (!texture)
    {
        wlr_log(WLR_DEBUG,
            "Failed to grab a texture from a buffer during shm screencopy");
        goto out;
    }

    {
        struct wlr_texture_read_pixels_options read_opts = {
            .data    = data,
            .format  = format,
            .stride  = static_cast<uint32_t>(stride),
            .src_box = frame->box,
        };
        ok = wlr_texture_read_pixels(texture, &read_opts);
    }

    wlr_texture_destroy(texture);

    if (ok)
    {
        censor_shm_pixels(frame, data, format, stride);
    }

out:
    wlr_buffer_end_data_ptr_access(frame->buffer);
    if (!ok)
    {
        wlr_log(WLR_DEBUG, "Failed to copy to destination during shm screencopy");
    }

    return ok;
}

void sparrow_censor_output_render_pass(struct wlr_output *output,
    const struct wlr_box *capture_box,
    struct wlr_render_pass *pass)
{
    const auto instance = Core::instance();
    if (!instance || !output || !capture_box || !pass)
    {
        return;
    }

    if (instance->is_output_sensitive(output))
    {
        struct wlr_render_rect_options rect_opts = {
            .box =
            {
                .x     = 0,
                .y     = 0,
                .width = capture_box->width,
                .height = capture_box->height,
            },
            .color =
            {
                .r = 0.0f,
                .g = 0.0f,
                .b = 0.0f,
                .a = 1.0f,
            },
            .clip = nullptr,
            .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
        };
        wlr_render_pass_add_rect(pass, &rect_opts);
        return;
    }

    SparrowView *view = nullptr;
    wl_list_for_each(view, &instance->views_list, link)
    {
        struct wlr_box view_buf_box = {};
        int corner_radius_px = 0;
        if (!sparrow_get_sensitive_view_buffer_box(
            instance, view, output, &view_buf_box, &corner_radius_px))
        {
            continue;
        }

        struct wlr_box intersect = {};
        if (!wlr_box_intersection(&intersect, &view_buf_box, capture_box))
        {
            continue;
        }

        int dst_x = intersect.x - capture_box->x;
        int dst_y = intersect.y - capture_box->y;
        int dst_w = intersect.width;
        int dst_h = intersect.height;

        if ((dst_w <= 0) || (dst_h <= 0))
        {
            continue;
        }

        if (dst_x < 0)
        {
            dst_w += dst_x;
            dst_x  = 0;
        }

        if (dst_y < 0)
        {
            dst_h += dst_y;
            dst_y  = 0;
        }

        if (dst_x + dst_w > capture_box->width)
        {
            dst_w = capture_box->width - dst_x;
        }

        if (dst_y + dst_h > capture_box->height)
        {
            dst_h = capture_box->height - dst_y;
        }

        if ((dst_w <= 0) || (dst_h <= 0))
        {
            continue;
        }

        pixman_region32_t rounded_clip;
        bool has_rounded = false;
        if (corner_radius_px > 0)
        {
            init_rounded_rect_region(&rounded_clip, view_buf_box.x - capture_box->x,
                view_buf_box.y - capture_box->y,
                view_buf_box.width, view_buf_box.height,
                corner_radius_px);
            has_rounded = true;
        }

        struct wlr_render_rect_options rect_opts = {
            .box =
            {
                .x     = dst_x,
                .y     = dst_y,
                .width = dst_w,
                .height = dst_h,
            },
            .color =
            {
                .r = 0.0f,
                .g = 0.0f,
                .b = 0.0f,
                .a = 1.0f,
            },
            .clip = has_rounded ? &rounded_clip : nullptr,
            .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
        };
        wlr_render_pass_add_rect(pass, &rect_opts);

        if (has_rounded)
        {
            pixman_region32_fini(&rounded_clip);
        }
    }
}

static bool frame_dma_copy(struct sparrow_screencopy_frame_v1 *frame,
    struct wlr_buffer *src_buffer)
{
    struct wlr_buffer *dst_buffer = frame->buffer;
    struct wlr_output *output     = frame->output;
    struct wlr_renderer *renderer = output->renderer;
    assert(renderer);

    struct wlr_texture *src_tex = wlr_texture_from_buffer(renderer, src_buffer);
    if (src_tex == nullptr)
    {
        wlr_log(WLR_DEBUG,
            "Failed to grab a texture from a buffer during dma screencopy");
        return false;
    }

    bool ok = false;
    const auto instance = Core::instance();
    struct wlr_render_pass *pass =
        wlr_renderer_begin_buffer_pass(renderer, dst_buffer, nullptr);
    if (!pass)
    {
        goto out;
    }

    if (instance && instance->is_output_sensitive(frame->output))
    {
        struct wlr_render_rect_options bg_opts = {
            .box =
            {
                .x     = 0,
                .y     = 0,
                .width = dst_buffer->width,
                .height = dst_buffer->height,
            },
            .color = {0.0f, 0.0f, 0.0f, 1.0f},
            .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
        };
        wlr_render_pass_add_rect(pass, &bg_opts);
    } else
    {
        struct wlr_render_texture_options tex_opts = {
            .texture = src_tex,
            .src_box =
            {
                .x     = (double)frame->box.x,
                .y     = (double)frame->box.y,
                .width = (double)frame->box.width,
                .height = (double)frame->box.height,
            },
            .dst_box =
            {
                .x     = 0,
                .y     = 0,
                .width = dst_buffer->width,
                .height = dst_buffer->height,
            },
            .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
        };
        wlr_render_pass_add_texture(pass, &tex_opts);
        sparrow_censor_output_render_pass(frame->output, &frame->box, pass);
    }

    ok = wlr_render_pass_submit(pass);

out:
    wlr_texture_destroy(src_tex);
    if (!ok)
    {
        wlr_log(WLR_DEBUG, "Failed to render to destination during dma screencopy");
    }

    return ok;
}

static void frame_handle_output_destroy(struct wl_listener *listener,
    void *data);

static void frame_handle_output_commit(struct wl_listener *listener,
    void *data)
{
    struct sparrow_screencopy_frame_v1 *frame =
        wl_container_of(listener, frame, output_commit);
    auto *event = static_cast<struct wlr_output_event_commit*>(data);
    struct wlr_output *output = frame->output;

    if ((event->state->committed & WLR_OUTPUT_STATE_ENABLED) &&
        !output->enabled)
    {
        goto err;
    }

    if (!(event->state->committed & WLR_OUTPUT_STATE_BUFFER))
    {
        return;
    }

    if (!frame->buffer)
    {
        return;
    }

    if (frame->with_damage)
    {
        struct sparrow_screencopy_damage *damage =
            screencopy_damage_get_or_create(frame->client, output);
        if (damage && pixman_region32_empty(&damage->damage))
        {
            return;
        }
    }

    wl_list_remove(&frame->output_commit.link);
    wl_list_init(&frame->output_commit.link);

    {
        struct wlr_buffer *src_buffer = event->state->buffer;
        if ((frame->box.x < 0) || (frame->box.y < 0) ||
            (frame->box.x + frame->box.width > src_buffer->width) ||
            (frame->box.y + frame->box.height > src_buffer->height))
        {
            goto err;
        }

        switch (frame->buffer_cap)
        {
          case WLR_BUFFER_CAP_DMABUF:
            if (!frame_dma_copy(frame, src_buffer))
            {
                goto err;
            }

            break;

          case WLR_BUFFER_CAP_DATA_PTR:
            if (!frame_shm_copy(frame, src_buffer))
            {
                goto err;
            }

            break;

          default:
            abort();
        }
    }

    zwlr_screencopy_frame_v1_send_flags(frame->resource, 0);
    frame_send_damage(frame);
    frame_send_ready(frame, &event->when);
    frame_destroy(frame);
    return;

err:
    zwlr_screencopy_frame_v1_send_failed(frame->resource);
    frame_destroy(frame);
}

static void frame_handle_output_destroy(struct wl_listener *listener,
    void *data)
{
    (void)data;
    struct sparrow_screencopy_frame_v1 *frame =
        wl_container_of(listener, frame, output_destroy);
    zwlr_screencopy_frame_v1_send_failed(frame->resource);
    frame_destroy(frame);
}

static void frame_handle_copy(struct wl_client *wl_client,
    struct wl_resource *frame_resource,
    struct wl_resource *buffer_resource)
{
    (void)wl_client;
    struct sparrow_screencopy_frame_v1 *frame =
        frame_from_resource(frame_resource);
    if (frame == nullptr)
    {
        return;
    }

    struct wlr_output *output = frame->output;
    if (!output || !output->enabled)
    {
        zwlr_screencopy_frame_v1_send_failed(frame->resource);
        frame_destroy(frame);
        return;
    }

    struct wlr_buffer *buffer = wlr_buffer_try_from_resource(buffer_resource);
    if (buffer == nullptr)
    {
        wl_resource_post_error(frame->resource,
            ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
            "invalid buffer");
        return;
    }

    if ((buffer->width != frame->box.width) ||
        (buffer->height != frame->box.height))
    {
        wl_resource_post_error(frame->resource,
            ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
            "invalid buffer dimensions");
        return;
    }

    if (frame->buffer != nullptr)
    {
        wl_resource_post_error(frame->resource,
            ZWLR_SCREENCOPY_FRAME_V1_ERROR_ALREADY_USED,
            "frame already used");
        return;
    }

    enum wlr_buffer_cap cap;
    struct wlr_dmabuf_attributes dmabuf = {};
    void *data = nullptr;
    uint32_t format = 0;
    size_t stride   = 0;

    if (wlr_buffer_get_dmabuf(buffer, &dmabuf))
    {
        cap = WLR_BUFFER_CAP_DMABUF;
        if (dmabuf.format != frame->dmabuf_format)
        {
            wl_resource_post_error(frame->resource,
                ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
                "invalid buffer format");
            return;
        }
    } else if (wlr_buffer_begin_data_ptr_access(buffer,
        WLR_BUFFER_DATA_PTR_ACCESS_WRITE,
        &data, &format, &stride))
    {
        wlr_buffer_end_data_ptr_access(buffer);
        cap = WLR_BUFFER_CAP_DATA_PTR;
        if (format != frame->shm_format)
        {
            wl_resource_post_error(frame->resource,
                ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
                "invalid buffer format");
            return;
        }

        if (stride != (size_t)frame->shm_stride)
        {
            wl_resource_post_error(frame->resource,
                ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
                "invalid buffer stride");
            return;
        }
    } else
    {
        wl_resource_post_error(frame->resource,
            ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
            "unsupported buffer type");
        return;
    }

    frame->buffer     = buffer;
    frame->buffer_cap = cap;

    wl_signal_add(&output->events.commit, &frame->output_commit);
    frame->output_commit.notify = frame_handle_output_commit;

    sparrow_damage_add_box(nullptr);
    wlr_output_update_needs_frame(output);
    wlr_output_schedule_frame(output);
    wlr_output_lock_attach_render(output, true);
    if (frame->overlay_cursor)
    {
        wlr_output_lock_software_cursors(output, true);
        frame->cursor_locked = true;
    }
}

static void frame_handle_copy_with_damage(struct wl_client *wl_client,
    struct wl_resource *frame_resource,
    struct wl_resource *buffer_resource)
{
    struct sparrow_screencopy_frame_v1 *frame =
        frame_from_resource(frame_resource);
    if (frame == nullptr)
    {
        return;
    }

    frame->with_damage = true;
    frame_handle_copy(wl_client, frame_resource, buffer_resource);
}

static void frame_handle_destroy(struct wl_client *wl_client,
    struct wl_resource *frame_resource)
{
    (void)wl_client;
    wl_resource_destroy(frame_resource);
}

static void frame_handle_resource_destroy(struct wl_resource *frame_resource)
{
    struct sparrow_screencopy_frame_v1 *frame =
        frame_from_resource(frame_resource);
    frame_destroy(frame);
}

static struct sparrow_screencopy_client *client_from_resource(struct wl_resource *resource)
{
    assert(wl_resource_instance_of(
        resource, &zwlr_screencopy_manager_v1_interface, &manager_impl));
    return static_cast<struct sparrow_screencopy_client*>(
        wl_resource_get_user_data(resource));
}

static void capture_output(struct wl_client *wl_client,
    struct sparrow_screencopy_client *client,
    uint32_t version, uint32_t id,
    int32_t overlay_cursor, struct wlr_output *output,
    const struct wlr_box *box)
{
    auto *frame = static_cast<struct sparrow_screencopy_frame_v1*>(
        calloc(1, sizeof(struct sparrow_screencopy_frame_v1)));
    if (frame == nullptr)
    {
        wl_client_post_no_memory(wl_client);
        return;
    }

    frame->output = output;
    frame->overlay_cursor = (overlay_cursor != 0);

    frame->resource = wl_resource_create(
        wl_client, &zwlr_screencopy_frame_v1_interface, version, id);
    if (frame->resource == nullptr)
    {
        free(frame);
        wl_client_post_no_memory(wl_client);
        return;
    }

    wl_resource_set_implementation(frame->resource, &frame_impl, frame,
        frame_handle_resource_destroy);

    if ((output == nullptr) || !output->enabled)
    {
        wl_resource_set_user_data(frame->resource, nullptr);
        zwlr_screencopy_frame_v1_send_failed(frame->resource);
        free(frame);
        return;
    }

    frame->client = client;
    client->ref++;

    wl_list_insert(&client->manager->frames, &frame->link);
    wl_list_init(&frame->output_commit.link);

    wl_signal_add(&output->events.destroy, &frame->output_destroy);
    frame->output_destroy.notify = frame_handle_output_destroy;

    struct wlr_renderer *renderer = output->renderer;
    assert(renderer);

    if (!wlr_output_configure_primary_swapchain(output, nullptr,
        &output->swapchain))
    {
        goto error;
    }

    {
        struct wlr_buffer *buffer = wlr_swapchain_acquire(output->swapchain);
        if (buffer == nullptr)
        {
            goto error;
        }

        struct wlr_texture *texture = wlr_texture_from_buffer(renderer, buffer);
        wlr_buffer_unlock(buffer);
        if (!texture)
        {
            goto error;
        }

        frame->shm_format = wlr_texture_preferred_read_format(texture);
        wlr_texture_destroy(texture);
    }

    if (frame->shm_format == DRM_FORMAT_INVALID)
    {
        wlr_log(WLR_ERROR,
            "Failed to capture output: no read format supported by renderer");
        goto error;
    }

    if (output->allocator &&
        (output->allocator->buffer_caps & WLR_BUFFER_CAP_DMABUF))
    {
        frame->dmabuf_format = output->render_format;
    } else
    {
        frame->dmabuf_format = DRM_FORMAT_INVALID;
    }

    {
        struct wlr_box buffer_box = {};
        if (box == nullptr)
        {
            buffer_box.width  = output->width;
            buffer_box.height = output->height;
        } else
        {
            int ow = 0, oh = 0;
            wlr_output_effective_resolution(output, &ow, &oh);
            buffer_box = *box;
            wlr_box_transform(&buffer_box, &buffer_box,
                wlr_output_transform_invert(output->transform), ow, oh);
            buffer_box.x     = (int)lround(buffer_box.x * output->scale);
            buffer_box.y     = (int)lround(buffer_box.y * output->scale);
            buffer_box.width = (int)lround(buffer_box.width * output->scale);
            buffer_box.height = (int)lround(buffer_box.height * output->scale);
        }

        frame->box = buffer_box;
        frame->shm_stride =
            buffer_box.width * drm_format_to_bytes_per_pixel(frame->shm_format);

        zwlr_screencopy_frame_v1_send_buffer(
            frame->resource, convert_drm_format_to_wl_shm(frame->shm_format),
            buffer_box.width, buffer_box.height, frame->shm_stride);

        if (version >= 3)
        {
            if (frame->dmabuf_format != DRM_FORMAT_INVALID)
            {
                zwlr_screencopy_frame_v1_send_linux_dmabuf(
                    frame->resource, frame->dmabuf_format, buffer_box.width,
                    buffer_box.height);
            }

            zwlr_screencopy_frame_v1_send_buffer_done(frame->resource);
        }
    }

    return;

error:
    zwlr_screencopy_frame_v1_send_failed(frame->resource);
    frame_destroy(frame);
}

static void manager_handle_capture_output(struct wl_client *wl_client,
    struct wl_resource *manager_resource,
    uint32_t id, int32_t overlay_cursor,
    struct wl_resource *output_resource)
{
    struct sparrow_screencopy_client *client =
        client_from_resource(manager_resource);
    uint32_t version = wl_resource_get_version(manager_resource);
    struct wlr_output *output = wlr_output_from_resource(output_resource);

    capture_output(wl_client, client, version, id, overlay_cursor, output,
        nullptr);
}

static void manager_handle_capture_output_region(
    struct wl_client *wl_client, struct wl_resource *manager_resource,
    uint32_t id, int32_t overlay_cursor, struct wl_resource *output_resource,
    int32_t x, int32_t y, int32_t width, int32_t height)
{
    struct sparrow_screencopy_client *client =
        client_from_resource(manager_resource);
    uint32_t version = wl_resource_get_version(manager_resource);
    struct wlr_output *output = wlr_output_from_resource(output_resource);

    struct wlr_box box = {
        .x     = x,
        .y     = y,
        .width = width,
        .height = height,
    };
    capture_output(wl_client, client, version, id, overlay_cursor, output, &box);
}

static void manager_handle_destroy(struct wl_client *wl_client,
    struct wl_resource *manager_resource)
{
    (void)wl_client;
    wl_resource_destroy(manager_resource);
}

static void manager_handle_resource_destroy(struct wl_resource *resource)
{
    struct sparrow_screencopy_client *client = client_from_resource(resource);
    client_unref(client);
}

static void manager_bind(struct wl_client *wl_client, void *data,
    uint32_t version, uint32_t id)
{
    auto *manager = static_cast<struct sparrow_screencopy_manager_v1*>(data);

    auto *client = static_cast<struct sparrow_screencopy_client*>(
        calloc(1, sizeof(struct sparrow_screencopy_client)));
    if (client == nullptr)
    {
        goto failure;
    }

    {
        struct wl_resource *resource = wl_resource_create(
            wl_client, &zwlr_screencopy_manager_v1_interface, version, id);
        if (resource == nullptr)
        {
            goto failure;
        }

        client->ref     = 1;
        client->manager = manager;
        wl_list_init(&client->damages);

        wl_resource_set_implementation(resource, &manager_impl, client,
            manager_handle_resource_destroy);
    }
    return;

failure:
    free(client);
    wl_client_post_no_memory(wl_client);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct sparrow_screencopy_manager_v1 *manager =
        wl_container_of(listener, manager, display_destroy);

    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
    free(manager);
}

struct sparrow_screencopy_manager_v1 *sparrow_screencopy_manager_v1_create(struct wl_display *display)
{
    auto *manager = static_cast<struct sparrow_screencopy_manager_v1*>(
        calloc(1, sizeof(struct sparrow_screencopy_manager_v1)));
    if (manager == nullptr)
    {
        return nullptr;
    }

    struct wl_global *global =
        wl_global_create(display, &zwlr_screencopy_manager_v1_interface,
                         SCREENCOPY_MANAGER_VERSION, manager, manager_bind);
    if (global == nullptr)
    {
        free(manager);
        return nullptr;
    }

    manager->global = global;
    wl_list_init(&manager->frames);

    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(display, &manager->display_destroy);

    return manager;
}
