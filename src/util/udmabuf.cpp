#include "util/udmabuf.hpp"
#include "core.hpp"

#include <drm_fourcc.h>
#include <fcntl.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wlr/types/wlr_shm.h>
#include <wlr/util/addon.h>

static int s_udmabuf_fd = -1;
static const char s_udmabuf_owner[] = "sparrow_udmabuf";

struct sparrow_udmabuf_addon
{
    struct wlr_addon addon;
    struct wlr_texture *texture;
    int dmabuf_fd;
};

static void addon_destroy(struct wlr_addon *addon)
{
    struct sparrow_udmabuf_addon *u_addon =
        wl_container_of(addon, u_addon, addon);
    wlr_addon_finish(addon);
    if (u_addon->texture)
    {
        wlr_texture_destroy(u_addon->texture);
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
        wlr_log(WLR_INFO, "[UDMABUF] Zero-copy wl_shm optimization active (/dev/udmabuf)");
    } else
    {
        wlr_log(WLR_INFO, "[UDMABUF] /dev/udmabuf not available, using CPU buffer upload for wl_shm");
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
    if (!surface || !surface->current.buffer)
    {
        return nullptr;
    }

    struct wlr_buffer *buffer = surface->current.buffer;

    // Check if we already have an addon on this buffer
    struct wlr_addon *addon = wlr_addon_find(&buffer->addons, s_udmabuf_owner, &addon_impl);
    if (addon)
    {
        struct sparrow_udmabuf_addon *u_addon =
            wl_container_of(addon, u_addon, addon);
        return u_addon->texture;
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

    size_t raw_size     = (size_t)shm_attrs.stride * (size_t)shm_attrs.height + page_offset;
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

    Core *instance = Core::instance();
    // Only import new textures into wlroots GLES renderer on the main Wayland thread
    // where the renderer's EGL context is active.
    if ((instance->main_thread_id != 0) && !pthread_equal(pthread_self(), instance->main_thread_id))
    {
        return nullptr;
    }

    struct wlr_texture *texture = wlr_texture_from_dmabuf(instance->renderer, &dmabuf_attrs);
    if (!texture)
    {
        close(dmabuf_fd);
        return nullptr;
    }

    struct sparrow_udmabuf_addon *u_addon = new sparrow_udmabuf_addon();
    u_addon->texture   = texture;
    u_addon->dmabuf_fd = dmabuf_fd;
    wlr_addon_init(&u_addon->addon, &buffer->addons, s_udmabuf_owner, &addon_impl);

    wlr_log(WLR_INFO,
        "[UDMABUF] Zero-copy hardware DMA-BUF texture created for wl_shm surface %p (%dx%d, fd=%d)",
        (void*)surface, shm_attrs.width, shm_attrs.height, dmabuf_fd);

    return texture;
}

struct wlr_texture *sparrow_surface_get_texture(struct wlr_surface *surface)
{
    if (!surface)
    {
        return nullptr;
    }

    struct wlr_texture *udma_tex = sparrow_udmabuf_get_or_import_texture(surface);
    if (udma_tex)
    {
        return udma_tex;
    }

    return wlr_surface_get_texture(surface);
}
