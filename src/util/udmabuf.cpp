#include "util/udmabuf.hpp"
#include "core.hpp"
#include <thread>
#include <vector>

#include <drm_fourcc.h>
#include <fcntl.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_shm.h>
#include <wlr/util/addon.h>

static int s_udmabuf_fd = -1;
static const char s_udmabuf_owner[] = "sparrow_udmabuf";

struct sparrow_udmabuf_addon
{
    struct wlr_addon addon;
    struct wlr_texture *texture;
    struct wlr_renderer *renderer;
    int dmabuf_fd;
};

static void addon_destroy(struct wlr_addon *addon)
{
    struct sparrow_udmabuf_addon *u_addon =
        wl_container_of(addon, u_addon, addon);
    const auto instance = Core::instance();
    wlr_addon_finish(addon);
    if (u_addon->texture)
    {
        if (instance && instance->renderer &&
            (u_addon->renderer == instance->renderer))
        {
            wlr_texture_destroy(u_addon->texture);
        }

        u_addon->texture = nullptr;
    }

    if (u_addon->dmabuf_fd >= 0)
    {
        close(u_addon->dmabuf_fd);
        u_addon->dmabuf_fd = -1;
    }

    delete u_addon;
}

static const struct wlr_addon_interface addon_impl = {
    .name    = "sparrow_udmabuf",
    .destroy = addon_destroy,
};

void sparrow_udmabuf_init()
{
    if (s_udmabuf_fd >= 0)
    {
        return;
    }

    s_udmabuf_fd = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
    if (s_udmabuf_fd >= 0)
    {
        wlr_log(WLR_INFO,
            "[UDMABUF] Zero-copy wl_shm optimization active (/dev/udmabuf)");
    } else
    {
        wlr_log(WLR_INFO, "[UDMABUF] /dev/udmabuf not available, using CPU buffer "
                          "upload for wl_shm");
    }
}

void sparrow_udmabuf_finish()
{
    if (s_udmabuf_fd >= 0)
    {
        close(s_udmabuf_fd);
        s_udmabuf_fd = -1;
    }
}

struct wlr_texture *sparrow_udmabuf_get_or_import_texture(struct wlr_surface *surface)
{
    if ((s_udmabuf_fd < 0) || !surface || !surface->buffer || !surface->buffer->source)
    {
        return nullptr;
    }

    struct wlr_buffer *buffer = surface->buffer->source;
    const auto instance = Core::instance();

    // Check if we already have an addon on this buffer
    struct wlr_addon *addon =
        wlr_addon_find(&buffer->addons, s_udmabuf_owner, &addon_impl);
    if (addon)
    {
        struct sparrow_udmabuf_addon *u_addon =
            wl_container_of(addon, u_addon, addon);
        if (instance && instance->renderer &&
            (u_addon->renderer != instance->renderer))
        {
            wlr_addon_finish(addon);
            if (u_addon->dmabuf_fd >= 0)
            {
                close(u_addon->dmabuf_fd);
                u_addon->dmabuf_fd = -1;
            }

            delete u_addon;
            addon = nullptr;
        } else
        {
            return u_addon->texture;
        }
    }

    if (s_udmabuf_fd < 0)
    {
        return nullptr;
    }

    struct wlr_shm_attributes shm_attrs = {};
    if (!wlr_buffer_get_shm(buffer, &shm_attrs))
    {
        return nullptr;
    }

    // Verify seals on the memfd
    int seals = fcntl(shm_attrs.fd, F_GET_SEALS);
    if (seals < 0)
    {
        return nullptr;
    }

    if (!(seals & F_SEAL_SHRINK))
    {
        if (fcntl(shm_attrs.fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0)
        {
            return nullptr;
        }
    }

    long page_size = sysconf(_SC_PAGE_SIZE);
    if (page_size <= 0)
    {
        page_size = 4096;
    }

    off_t offset_aligned = shm_attrs.offset & ~(page_size - 1);
    off_t page_offset    = shm_attrs.offset - offset_aligned;

    size_t raw_size =
        (size_t)shm_attrs.stride * (size_t)shm_attrs.height + page_offset;
    size_t size_aligned = (raw_size + page_size - 1) & ~(page_size - 1);

    struct udmabuf_create udmabuf_create = {
        .memfd  = (uint32_t)shm_attrs.fd,
        .flags  = UDMABUF_FLAGS_CLOEXEC,
        .offset = (uint64_t)offset_aligned,
        .size   = (uint64_t)size_aligned,
    };

    int dmabuf_fd = ioctl(s_udmabuf_fd, UDMABUF_CREATE, &udmabuf_create);
    if (dmabuf_fd < 0)
    {
        return nullptr;
    }

    struct wlr_dmabuf_attributes dmabuf_attrs = {
        .width    = shm_attrs.width,
        .height   = shm_attrs.height,
        .format   = shm_attrs.format,
        .modifier = DRM_FORMAT_MOD_LINEAR,
        .n_planes = 1,
        .offset   = {(uint32_t)page_offset},
        .stride   = {(uint32_t)shm_attrs.stride},
        .fd = {dmabuf_fd},
    };

    // Only import new textures into wlroots GLES renderer on the main Wayland
    // thread where the renderer's EGL context is active.
    if ((instance->main_thread_id != std::thread::id()) &&
        (std::this_thread::get_id() != instance->main_thread_id))
    {
        return nullptr;
    }

    struct wlr_texture *texture =
        wlr_texture_from_dmabuf(instance->renderer, &dmabuf_attrs);
    if (!texture)
    {
        close(dmabuf_fd);
        return nullptr;
    }

    struct sparrow_udmabuf_addon *u_addon = new sparrow_udmabuf_addon();
    u_addon->texture   = texture;
    u_addon->renderer  = instance->renderer;
    u_addon->dmabuf_fd = dmabuf_fd;
    wlr_addon_init(&u_addon->addon, &buffer->addons, s_udmabuf_owner,
        &addon_impl);

    wlr_log(WLR_INFO,
        "[UDMABUF] Zero-copy hardware DMA-BUF texture created for wl_shm "
        "surface %p (%dx%d, fd=%d)",
        (void*)surface, shm_attrs.width, shm_attrs.height, dmabuf_fd);

    return texture;
}

extern "C" struct wlr_client_buffer *wlr_client_buffer_create(struct wlr_buffer *buffer,
    struct wlr_renderer *renderer);

void sparrow_surface_rehydrate_texture(struct wlr_surface *surface)
{
    if (!surface || (surface->buffer == nullptr))
    {
        return;
    }

    const auto instance = Core::instance();
    if (!instance || !instance->renderer)
    {
        return;
    }

    // Only import textures on main Wayland thread where wlr_renderer EGL context
    // is current
    if ((instance->main_thread_id != std::thread::id()) &&
        (std::this_thread::get_id() != instance->main_thread_id))
    {
        return;
    }

    struct wlr_texture *old_tex = surface->buffer->texture;
    if ((old_tex != nullptr) && (old_tex->renderer == instance->renderer))
    {
        return;
    }

    struct wlr_buffer *source = surface->buffer->source;
    if (!source && surface->current.buffer)
    {
        struct wlr_client_buffer *cb =
            wlr_client_buffer_get(surface->current.buffer);
        source = (cb && cb->source) ? cb->source : surface->current.buffer;
    }

    // 1. Hardware DMA-BUF buffers: re-import directly from the DMA-BUF source
    // into the new renderer (DMA-BUF contents are not incrementally patched via
    // glTexSubImage2D).
    struct wlr_dmabuf_attributes dmabuf_attrs = {};
    if ((source != nullptr) && wlr_buffer_get_dmabuf(source, &dmabuf_attrs))
    {
        struct wlr_client_buffer *new_buffer =
            wlr_client_buffer_create(source, instance->renderer);
        if (new_buffer != nullptr)
        {
            struct wlr_client_buffer *old_buffer = surface->buffer;
            surface->buffer = new_buffer;
            wlr_buffer_unlock(&old_buffer->base);
            wlr_log(WLR_INFO,
                "[GPU-RESET] Re-imported DMA-BUF client buffer for surface %p "
                "(%dx%d, tex=%p)",
                (void*)surface, surface->current.width, surface->current.height,
                (void*)new_buffer->texture);
            return;
        }
    }

    // 2. For wl_shm / pixel textures: if the old texture is still valid on the
    // previous renderer (soft reset), read back its exact composited pixels
    // (which include all incremental wlr_client_buffer_apply_damage updates)
    // and upload them into a mutable texture on the new renderer.
    if ((old_tex != nullptr) && (old_tex->width > 0) && (old_tex->height > 0))
    {
        uint32_t fmt = DRM_FORMAT_ARGB8888;
        struct wlr_shm_attributes shm_attrs = {};
        if ((source != nullptr) && wlr_buffer_get_shm(source, &shm_attrs))
        {
            fmt = shm_attrs.format;
        } else if (surface->WLR_PRIVATE.opaque)
        {
            fmt = DRM_FORMAT_XRGB8888;
        }

        const uint32_t w = old_tex->width;
        const uint32_t h = old_tex->height;
        const uint32_t stride = w * 4;
        std::vector<uint8_t> pixels((size_t)stride * (size_t)h);

        struct wlr_texture_read_pixels_options read_opts = {
            .data    = pixels.data(),
            .format  = fmt,
            .stride  = stride,
            .dst_x   = 0,
            .dst_y   = 0,
            .src_box = {0, 0, (int)w, (int)h},
        };

        bool read_ok = wlr_texture_read_pixels(old_tex, &read_opts);
        if (!read_ok)
        {
            uint32_t pref_fmt = wlr_texture_preferred_read_format(old_tex);
            if ((pref_fmt != DRM_FORMAT_INVALID) && (pref_fmt != fmt))
            {
                fmt = pref_fmt;
                read_opts.format = fmt;
                read_ok = wlr_texture_read_pixels(old_tex, &read_opts);
            }
        }

        if (read_ok)
        {
            struct wlr_texture *new_tex = wlr_texture_from_pixels(
                instance->renderer, fmt, stride, w, h, pixels.data());
            if (new_tex != nullptr)
            {
                wl_list_remove(&surface->buffer->WLR_PRIVATE.renderer_destroy.link);
                wl_signal_add(&instance->renderer->events.destroy,
                    &surface->buffer->WLR_PRIVATE.renderer_destroy);
                surface->buffer->texture = new_tex;
                wlr_texture_destroy(old_tex);
                wlr_log(WLR_INFO,
                    "[GPU-RESET] Migrated composited SHM texture for surface %p "
                    "(%ux%u, fmt=0x%x, tex=%p)",
                    (void*)surface, w, h, fmt, (void*)new_tex);
                return;
            }
        }
    }

    // 3. Fallback (e.g. hard GPU reset where old_tex could not be read back):
    // re-create the client buffer from the SHM source buffer on the new renderer.
    if (source != nullptr)
    {
        struct wlr_client_buffer *new_buffer =
            wlr_client_buffer_create(source, instance->renderer);
        if (new_buffer != nullptr)
        {
            struct wlr_client_buffer *old_buffer = surface->buffer;
            surface->buffer = new_buffer;
            wlr_buffer_unlock(&old_buffer->base);
            wlr_log(WLR_INFO,
                "[GPU-RESET] Re-created client buffer for surface %p (%dx%d, "
                "tex=%p)",
                (void*)surface, surface->current.width, surface->current.height,
                (void*)new_buffer->texture);
        } else
        {
            wlr_log(WLR_ERROR,
                "[GPU-RESET] wlr_client_buffer_create failed for surface %p "
                "source=%p",
                (void*)surface, (void*)source);
        }
    } else
    {
        wlr_log(WLR_ERROR, "[GPU-RESET] surface %p has buffer but source is NULL!",
            (void*)surface);
    }
}

struct wlr_texture *sparrow_surface_get_texture(struct wlr_surface *surface)
{
    if (!surface || !surface->buffer)
    {
        return nullptr;
    }

    struct wlr_texture *udma_tex = sparrow_udmabuf_get_or_import_texture(surface);
    if (udma_tex)
    {
        return udma_tex;
    }

    struct wlr_texture *tex = wlr_surface_get_texture(surface);
    if (tex)
    {
        return tex;
    }

    sparrow_surface_rehydrate_texture(surface);
    return wlr_surface_get_texture(surface);
}
