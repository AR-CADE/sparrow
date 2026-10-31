#include <assert.h>
#include <sparrow/nonstd/wlroots-full.hpp>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/allocator.h>
#include <wlr/render/dmabuf.h>
#include <wlr/util/log.h>

#include "../core.hpp"
#include "../screencopy/sparrow_screencopy.hpp"
#include "../surface/view.hpp"
#include "sparrow_export_dmabuf.hpp"
#include "wlr-export-dmabuf-unstable-v1-protocol.h"

#define EXPORT_DMABUF_MANAGER_VERSION 1

struct sparrow_export_dmabuf_manager_v1
{
    struct wl_global *global;
    struct wl_list frames; // sparrow_export_dmabuf_frame_v1.link
    struct wl_listener display_destroy;
};

struct sparrow_export_dmabuf_frame_v1
{
    struct wl_resource *resource;
    struct sparrow_export_dmabuf_manager_v1 *manager;
    struct wl_list link; // sparrow_export_dmabuf_manager_v1.frames

    struct wlr_output *output;
    struct wlr_buffer *censored_buffer;
    bool cursor_locked;

    struct wl_listener output_commit;
    struct wl_listener output_destroy;
};

static void frame_handle_destroy(struct wl_client *client,
    struct wl_resource *resource);

static const struct zwlr_export_dmabuf_frame_v1_interface frame_impl = {
    .destroy = frame_handle_destroy,
};

static struct sparrow_export_dmabuf_frame_v1 *frame_from_resource(struct wl_resource *resource)
{
    assert(wl_resource_instance_of(
        resource, &zwlr_export_dmabuf_frame_v1_interface, &frame_impl));
    return static_cast<struct sparrow_export_dmabuf_frame_v1*>(
        wl_resource_get_user_data(resource));
}

static void frame_destroy(struct sparrow_export_dmabuf_frame_v1 *frame)
{
    if (frame == nullptr)
    {
        return;
    }

    if (frame->output != nullptr)
    {
        wlr_output_lock_attach_render(frame->output, false);
        if (frame->cursor_locked)
        {
            wlr_output_lock_software_cursors(frame->output, false);
        }
    }

    if (frame->censored_buffer != nullptr)
    {
        wlr_buffer_drop(frame->censored_buffer);
        frame->censored_buffer = nullptr;
    }

    wl_list_remove(&frame->link);
    wl_list_remove(&frame->output_commit.link);
    wl_list_remove(&frame->output_destroy.link);
    wl_resource_set_user_data(frame->resource, nullptr);
    free(frame);
}

static void frame_handle_destroy(struct wl_client *client,
    struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void frame_handle_resource_destroy(struct wl_resource *resource)
{
    struct sparrow_export_dmabuf_frame_v1 *frame = frame_from_resource(resource);
    frame_destroy(frame);
}

static struct wlr_buffer *create_censored_dmabuf_buffer(struct wlr_output *output,
    struct wlr_buffer *src_buffer)
{
    if (!output || !output->renderer || !output->allocator ||
        !output->swapchain || !src_buffer)
    {
        return nullptr;
    }

    struct wlr_buffer *dst_buffer = wlr_allocator_create_buffer(
        output->allocator, src_buffer->width, src_buffer->height,
        &output->swapchain->format);
    if (!dst_buffer)
    {
        return nullptr;
    }

    struct wlr_texture *src_tex =
        wlr_texture_from_buffer(output->renderer, src_buffer);
    if (!src_tex)
    {
        wlr_buffer_drop(dst_buffer);
        return nullptr;
    }

    struct wlr_render_pass *pass =
        wlr_renderer_begin_buffer_pass(output->renderer, dst_buffer, nullptr);
    if (!pass)
    {
        wlr_texture_destroy(src_tex);
        wlr_buffer_drop(dst_buffer);
        return nullptr;
    }

    const auto instance = Core::instance();
    if (instance && instance->is_output_sensitive(output))
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
                .x     = 0.0,
                .y     = 0.0,
                .width = (double)src_buffer->width,
                .height = (double)src_buffer->height,
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

        struct wlr_box full_box = {
            .x     = 0,
            .y     = 0,
            .width = dst_buffer->width,
            .height = dst_buffer->height,
        };
        sparrow_censor_output_render_pass(output, &full_box, pass);
    }

    bool ok = wlr_render_pass_submit(pass);
    wlr_texture_destroy(src_tex);

    if (!ok)
    {
        wlr_buffer_drop(dst_buffer);
        return nullptr;
    }

    return dst_buffer;
}

static void frame_output_handle_commit(struct wl_listener *listener,
    void *data)
{
    struct sparrow_export_dmabuf_frame_v1 *frame =
        wl_container_of(listener, frame, output_commit);
    auto *event = static_cast<struct wlr_output_event_commit*>(data);

    if (!(event->state->committed & WLR_OUTPUT_STATE_BUFFER))
    {
        return;
    }

    wl_list_remove(&frame->output_commit.link);
    wl_list_init(&frame->output_commit.link);

    struct wlr_buffer *export_buffer = event->state->buffer;

    const auto instance = Core::instance();
    if (instance && frame->output)
    {
        bool has_sensitive = instance->is_output_sensitive(frame->output);
        if (!has_sensitive)
        {
            SparrowView *view = nullptr;
            wl_list_for_each(view, &instance->views_list, link)
            {
                struct wlr_box dummy = {};
                if (sparrow_get_sensitive_view_buffer_box(instance, view, frame->output,
                    &dummy))
                {
                    has_sensitive = true;
                    break;
                }
            }
        }

        if (has_sensitive)
        {
            frame->censored_buffer =
                create_censored_dmabuf_buffer(frame->output, event->state->buffer);
            if (!frame->censored_buffer)
            {
                zwlr_export_dmabuf_frame_v1_send_cancel(
                    frame->resource,
                    ZWLR_EXPORT_DMABUF_FRAME_V1_CANCEL_REASON_TEMPORARY);
                frame_destroy(frame);
                return;
            }

            export_buffer = frame->censored_buffer;
        }
    }

    struct wlr_dmabuf_attributes attribs = {};
    if (!wlr_buffer_get_dmabuf(export_buffer, &attribs))
    {
        zwlr_export_dmabuf_frame_v1_send_cancel(
            frame->resource, ZWLR_EXPORT_DMABUF_FRAME_V1_CANCEL_REASON_TEMPORARY);
        frame_destroy(frame);
        return;
    }

    uint32_t frame_flags = ZWLR_EXPORT_DMABUF_FRAME_V1_FLAGS_TRANSIENT;
    uint32_t mod_high    = attribs.modifier >> 32;
    uint32_t mod_low     = attribs.modifier & 0xFFFFFFFF;
    zwlr_export_dmabuf_frame_v1_send_frame(
        frame->resource, attribs.width, attribs.height, 0, 0, 0, frame_flags,
        attribs.format, mod_high, mod_low, attribs.n_planes);

    for (int i = 0; i < attribs.n_planes; ++i)
    {
        off_t size = lseek(attribs.fd[i], 0, SEEK_END);
        zwlr_export_dmabuf_frame_v1_send_object(frame->resource, i, attribs.fd[i],
            size, attribs.offset[i],
            attribs.stride[i], i);
    }

    time_t tv_sec = event->when.tv_sec;
    uint32_t tv_sec_hi = (sizeof(tv_sec) > 4) ? (uint32_t)(tv_sec >> 32) : 0;
    uint32_t tv_sec_lo = (uint32_t)(tv_sec & 0xFFFFFFFF);
    zwlr_export_dmabuf_frame_v1_send_ready(frame->resource, tv_sec_hi, tv_sec_lo,
        event->when.tv_nsec);
    frame_destroy(frame);
}

static void frame_output_handle_destroy(struct wl_listener *listener,
    void *data)
{
    (void)data;
    struct sparrow_export_dmabuf_frame_v1 *frame =
        wl_container_of(listener, frame, output_destroy);
    frame_destroy(frame);
}

static void manager_handle_capture_output(struct wl_client *client,
    struct wl_resource *manager_resource,
    uint32_t id, int32_t overlay_cursor,
    struct wl_resource *output_resource);
static void manager_handle_destroy(struct wl_client *client,
    struct wl_resource *manager_resource);

static const struct zwlr_export_dmabuf_manager_v1_interface manager_impl = {
    .capture_output = manager_handle_capture_output,
    .destroy = manager_handle_destroy,
};

static struct sparrow_export_dmabuf_manager_v1 *manager_from_resource(struct wl_resource *resource)
{
    assert(wl_resource_instance_of(
        resource, &zwlr_export_dmabuf_manager_v1_interface, &manager_impl));
    return static_cast<struct sparrow_export_dmabuf_manager_v1*>(
        wl_resource_get_user_data(resource));
}

static void manager_handle_capture_output(struct wl_client *client,
    struct wl_resource *manager_resource,
    uint32_t id, int32_t overlay_cursor,
    struct wl_resource *output_resource)
{
    struct sparrow_export_dmabuf_manager_v1 *manager =
        manager_from_resource(manager_resource);
    struct wlr_output *output = wlr_output_from_resource(output_resource);

    auto *frame = static_cast<struct sparrow_export_dmabuf_frame_v1*>(
        calloc(1, sizeof(struct sparrow_export_dmabuf_frame_v1)));
    if (frame == nullptr)
    {
        wl_resource_post_no_memory(manager_resource);
        return;
    }

    frame->manager = manager;
    wl_list_init(&frame->output_commit.link);
    wl_list_init(&frame->output_destroy.link);

    uint32_t version = wl_resource_get_version(manager_resource);
    frame->resource = wl_resource_create(
        client, &zwlr_export_dmabuf_frame_v1_interface, version, id);
    if (frame->resource == nullptr)
    {
        wl_client_post_no_memory(client);
        free(frame);
        return;
    }

    wl_resource_set_implementation(frame->resource, &frame_impl, frame,
        frame_handle_resource_destroy);

    wl_list_insert(&manager->frames, &frame->link);

    if ((output == nullptr) || !output->enabled)
    {
        zwlr_export_dmabuf_frame_v1_send_cancel(
            frame->resource, ZWLR_EXPORT_DMABUF_FRAME_V1_CANCEL_REASON_PERMANENT);
        frame_destroy(frame);
        return;
    }

    frame->output = output;

    wlr_output_lock_attach_render(frame->output, true);
    if (overlay_cursor)
    {
        wlr_output_lock_software_cursors(frame->output, true);
        frame->cursor_locked = true;
    }

    wl_list_remove(&frame->output_commit.link);
    wl_signal_add(&output->events.commit, &frame->output_commit);
    frame->output_commit.notify = frame_output_handle_commit;
    wl_signal_add(&output->events.destroy, &frame->output_destroy);
    frame->output_destroy.notify = frame_output_handle_destroy;

    wlr_output_update_needs_frame(output);
}

static void manager_handle_destroy(struct wl_client *client,
    struct wl_resource *manager_resource)
{
    (void)client;
    wl_resource_destroy(manager_resource);
}

static void manager_bind(struct wl_client *client, void *data, uint32_t version,
    uint32_t id)
{
    auto *manager = static_cast<struct sparrow_export_dmabuf_manager_v1*>(data);

    struct wl_resource *resource = wl_resource_create(
        client, &zwlr_export_dmabuf_manager_v1_interface, version, id);
    if (resource == nullptr)
    {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &manager_impl, manager, nullptr);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct sparrow_export_dmabuf_manager_v1 *manager =
        wl_container_of(listener, manager, display_destroy);

    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
    free(manager);
}

struct sparrow_export_dmabuf_manager_v1 *sparrow_export_dmabuf_manager_v1_create(struct wl_display *display)
{
    auto *manager = static_cast<struct sparrow_export_dmabuf_manager_v1*>(
        calloc(1, sizeof(struct sparrow_export_dmabuf_manager_v1)));
    if (manager == nullptr)
    {
        return nullptr;
    }

    wl_list_init(&manager->frames);

    manager->global =
        wl_global_create(display, &zwlr_export_dmabuf_manager_v1_interface,
            EXPORT_DMABUF_MANAGER_VERSION, manager, manager_bind);
    if (manager->global == nullptr)
    {
        free(manager);
        return nullptr;
    }

    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(display, &manager->display_destroy);

    return manager;
}
