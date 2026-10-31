#include "wayland_window.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ipc_client.hpp"
#include "sparrow-ipc-v1-client-protocol.h"

// --- Wayland Protocol Callbacks ---

static void data_offer_handle_offer(void *data, struct wl_data_offer *offer, const char *mime_type)
{
    auto *self = static_cast<WaylandWindow*>(data);
    if (self)
    {
        self->handle_offer_mime_type(offer, mime_type);
    }
}

static void data_offer_handle_source_actions(void *data, struct wl_data_offer *offer, uint32_t source_actions)
{
    (void)data;
    (void)offer;
    (void)source_actions;
}

static void data_offer_handle_action(void *data, struct wl_data_offer *offer, uint32_t dnd_action)
{
    (void)data;
    (void)offer;
    (void)dnd_action;
}

static const struct wl_data_offer_listener data_offer_listener = {
    .offer = data_offer_handle_offer,
    .source_actions = data_offer_handle_source_actions,
    .action = data_offer_handle_action,
};

static void data_device_handle_data_offer(void *data, struct wl_data_device *data_device,
    struct wl_data_offer *id)
{
    (void)data_device;
    auto *self = static_cast<WaylandWindow*>(data);
    if (self)
    {
        self->handle_data_offer(id);
    }
}

static void data_device_handle_enter(void *data, struct wl_data_device *data_device, uint32_t serial,
    struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *id)
{
    (void)data;
    (void)data_device;
    (void)serial;
    (void)surface;
    (void)x;
    (void)y;
    (void)id;
}

static void data_device_handle_leave(void *data, struct wl_data_device *data_device)
{
    (void)data;
    (void)data_device;
}

static void data_device_handle_motion(void *data, struct wl_data_device *data_device, uint32_t time,
    wl_fixed_t x, wl_fixed_t y)
{
    (void)data;
    (void)data_device;
    (void)time;
    (void)x;
    (void)y;
}

static void data_device_handle_drop(void *data, struct wl_data_device *data_device)
{
    (void)data;
    (void)data_device;
}

static void data_device_handle_selection(void *data, struct wl_data_device *data_device,
    struct wl_data_offer *id)
{
    (void)data_device;
    auto *self = static_cast<WaylandWindow*>(data);
    if (self)
    {
        self->handle_selection(id);
    }
}

static const struct wl_data_device_listener data_device_listener = {
    .data_offer = data_device_handle_data_offer,
    .enter  = data_device_handle_enter,
    .leave  = data_device_handle_leave,
    .motion = data_device_handle_motion,
    .drop   = data_device_handle_drop,
    .selection = data_device_handle_selection,
};

static void data_source_handle_target(void *data, struct wl_data_source *source, const char *mime_type)
{
    (void)data;
    (void)source;
    (void)mime_type;
}

static void data_source_handle_send(void *data, struct wl_data_source *source, const char *mime_type,
    int32_t fd)
{
    auto *self = static_cast<WaylandWindow*>(data);
    if (self)
    {
        self->handle_data_source_send(source, mime_type, fd);
    } else if (fd >= 0)
    {
        close(fd);
    }
}

static void data_source_handle_cancelled(void *data, struct wl_data_source *source)
{
    auto *self = static_cast<WaylandWindow*>(data);
    if (self)
    {
        self->handle_data_source_cancelled(source);
    }
}

static void data_source_handle_dnd_drop_performed(void *data, struct wl_data_source *source)
{
    (void)data;
    (void)source;
}

static void data_source_handle_dnd_finished(void *data, struct wl_data_source *source)
{
    (void)data;
    (void)source;
}

static void data_source_handle_action(void *data, struct wl_data_source *source, uint32_t dnd_action)
{
    (void)data;
    (void)source;
    (void)dnd_action;
}

static const struct wl_data_source_listener data_source_listener = {
    .target = data_source_handle_target,
    .send   = data_source_handle_send,
    .cancelled = data_source_handle_cancelled,
    .dnd_drop_performed = data_source_handle_dnd_drop_performed,
    .dnd_finished = data_source_handle_dnd_finished,
    .action = data_source_handle_action,
};

static const struct sparrow_ipc_manager_v1_listener ipc_manager_listener = {
    .ipc_channel = [] (void *data, struct sparrow_ipc_manager_v1 *manager, int32_t fd)
    {
        (void)manager;
        auto *self = static_cast<WaylandWindow*>(data);
        if (self)
        {
            self->handle_ipc_channel(fd);
        }
    },
};

static void registry_handle_global(void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
    auto *self = static_cast<WaylandWindow*>(data);
    self->handle_registry_global(registry, name, interface, version);
}

static void registry_handle_global_remove(void *data,
    struct wl_registry *registry,
    uint32_t name)
{
    auto *self = static_cast<WaylandWindow*>(data);
    self->handle_registry_global_remove(registry, name);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void xdg_wm_base_handle_ping(void *data,
    struct xdg_wm_base *xdg_wm_base,
    uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

static void xdg_surface_handle_configure(void *data,
    struct xdg_surface *surface,
    uint32_t serial)
{
    auto *self = static_cast<WaylandWindow*>(data);
    self->handle_xdg_surface_configure(surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data,
    struct xdg_toplevel *toplevel,
    int32_t width,
    int32_t height,
    struct wl_array *states)
{
    auto *self = static_cast<WaylandWindow*>(data);
    self->handle_xdg_toplevel_configure(toplevel, width, height, states);
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *toplevel)
{
    auto *self = static_cast<WaylandWindow*>(data);
    self->handle_xdg_toplevel_close(toplevel);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close     = xdg_toplevel_handle_close,
};

static void surface_handle_enter(void *data, struct wl_surface *surface, struct wl_output *output)
{
    auto *self = static_cast<WaylandWindow*>(data);
    self->handle_surface_enter(surface, output);
}

static void surface_handle_leave(void *data, struct wl_surface *surface, struct wl_output *output)
{
    auto *self = static_cast<WaylandWindow*>(data);
    self->handle_surface_leave(surface, output);
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_handle_enter,
    .leave = surface_handle_leave,
};

static struct wl_buffer *create_black_shm_buffer(struct wl_shm *shm, int32_t width, int32_t height)
{
    if (!shm || (width <= 0) || (height <= 0))
    {
        return nullptr;
    }

    int stride = width * 4;
    int size   = stride * height;
    int fd     = memfd_create("sparrow-lock-black-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0)
    {
        return nullptr;
    }

    if (ftruncate(fd, size) < 0)
    {
        close(fd);
        return nullptr;
    }

    void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        close(fd);
        return nullptr;
    }

    memset(data, 0, size);
    munmap(data, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static void session_lock_handle_locked(void *data, struct ext_session_lock_v1 *lock)
{
    (void)lock;
    auto *self = static_cast<WaylandWindow*>(data);
    if (self)
    {
        self->handle_session_lock_locked();
    }
}

static void session_lock_handle_finished(void *data, struct ext_session_lock_v1 *lock)
{
    (void)lock;
    auto *self = static_cast<WaylandWindow*>(data);
    if (self)
    {
        self->handle_session_lock_finished();
    }
}

static const struct ext_session_lock_v1_listener session_lock_listener = {
    .locked   = session_lock_handle_locked,
    .finished = session_lock_handle_finished,
};

static void session_lock_surface_handle_configure(void *data,
    struct ext_session_lock_surface_v1 *lock_surface,
    uint32_t serial, uint32_t width, uint32_t height)
{
    auto *self = static_cast<WaylandWindow*>(data);
    if (self)
    {
        self->handle_session_lock_surface_configure(lock_surface, serial, width, height);
    }
}

static const struct ext_session_lock_surface_v1_listener session_lock_surface_listener = {
    .configure = session_lock_surface_handle_configure,
};

static void secondary_lock_surface_handle_configure(void *data,
    struct ext_session_lock_surface_v1 *lock_surface,
    uint32_t serial, uint32_t width, uint32_t height)
{
    (void)lock_surface;
    auto *sec = static_cast<SecondaryLockSurface*>(data);
    if (sec && sec->window)
    {
        sec->window->handle_secondary_lock_surface_configure(sec, serial, width, height);
    }
}

static const struct ext_session_lock_surface_v1_listener secondary_lock_surface_listener = {
    .configure = secondary_lock_surface_handle_configure,
};

// --- WaylandWindow Implementation ---

WaylandWindow::WaylandWindow() = default;

WaylandWindow::~WaylandWindow()
{
    shutdown();
}

bool WaylandWindow::init(const std::string & app_id,
    const std::string & title,
    int32_t initial_width,
    int32_t initial_height,
    bool fullscreen,
    bool maximized,
    bool enable_egl,
    bool session_lock)
{
    width_  = initial_width;
    height_ = initial_height;
    fullscreen_ = fullscreen;
    maximized_  = maximized;
    app_id_     = app_id;
    title_ = title;
    enable_egl_ = enable_egl;
    session_lock_mode_ = session_lock;

    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    display_ = wl_display_connect(nullptr);
    if (!display_)
    {
        fprintf(stderr,
            "[sparrow-app-runner] Failed to connect to Wayland display (check $WAYLAND_DISPLAY)\n");
        return false;
    }

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &registry_listener, this);

    // Initial roundtrip to discover all globals
    wl_display_roundtrip(display_);

    if (!compositor_)
    {
        fprintf(stderr, "[sparrow-app-runner] Missing required Wayland global: wl_compositor\n");
        return false;
    }

    if (session_lock_mode_)
    {
        if (!lock_manager_)
        {
            fprintf(stderr,
                "[sparrow-app-runner] Missing required Wayland global: ext_session_lock_manager_v1\n");
            return false;
        }

        if (outputs_.empty())
        {
            fprintf(stderr,
                "[sparrow-app-runner] No wl_output discovered for session lock\n");
            return false;
        }

        printf("[sparrow-app-runner] Initializing session lock across %zu output(s)...\n",
            outputs_.size());
        fflush(stdout);

        // 1. Create primary wl_surface
        surface_ = wl_compositor_create_surface(compositor_);
        if (!surface_)
        {
            fprintf(stderr, "[sparrow-app-runner] Failed to create primary wl_surface\n");
            return false;
        }

        wl_surface_add_listener(surface_, &surface_listener, this);

        // 2. Lock session
        session_lock_ = ext_session_lock_manager_v1_lock(lock_manager_);
        if (!session_lock_)
        {
            fprintf(stderr, "[sparrow-app-runner] Failed to lock session via ext_session_lock_manager_v1\n");
            return false;
        }

        ext_session_lock_v1_add_listener(session_lock_, &session_lock_listener, this);

        // 3. Create lock surfaces for each output
        for (size_t i = 0; i < outputs_.size(); ++i)
        {
            struct wl_output *out = outputs_[i];
            if (i == 0)
            {
                lock_surface_ = ext_session_lock_v1_get_lock_surface(
                    session_lock_, surface_, out);
                if (!lock_surface_)
                {
                    fprintf(stderr, "[sparrow-app-runner] Failed to create primary lock surface\n");
                    return false;
                }

                ext_session_lock_surface_v1_add_listener(
                    lock_surface_, &session_lock_surface_listener, this);
            } else
            {
                struct wl_surface *sec_surf = wl_compositor_create_surface(compositor_);
                struct ext_session_lock_surface_v1 *sec_lock =
                    ext_session_lock_v1_get_lock_surface(session_lock_, sec_surf, out);
                SecondaryLockSurface sec_entry;
                sec_entry.window  = this;
                sec_entry.output  = out;
                sec_entry.surface = sec_surf;
                sec_entry.lock_surface = sec_lock;
                secondary_lock_surfaces_.push_back(sec_entry);
                ext_session_lock_surface_v1_add_listener(
                    sec_lock, &secondary_lock_surface_listener, &secondary_lock_surfaces_.back());
            }
        }

        // 4. Wait for configure event so width_ and height_ are set
        while (!configured_ && running_)
        {
            if (wl_display_dispatch(display_) == -1)
            {
                fprintf(stderr, "[sparrow-app-runner] Error waiting for session lock configure\n");
                return false;
            }
        }

        // 5. Initialize EGL with actual output dimensions
        if (enable_egl_)
        {
            if (!init_egl())
            {
                fprintf(stderr, "[sparrow-app-runner] Failed to initialize EGL for lock surface\n");
                return false;
            }

            // Perform initial clear & swap to attach buffer and commit primary surface
            if (eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_))
            {
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                eglSwapBuffers(egl_display_, egl_surface_);
                eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            }
        }

        // 6. Wait for compositor to confirm locked state
        while (!session_locked_ && running_)
        {
            if (wl_display_dispatch(display_) == -1)
            {
                fprintf(stderr, "[sparrow-app-runner] Error waiting for session locked confirmation\n");
                return false;
            }
        }

        return session_locked_;
    }

    if (!xdg_wm_base_)
    {
        fprintf(stderr, "[sparrow-app-runner] Missing required Wayland global: xdg_wm_base\n");
        return false;
    }

    // Create wayland surface
    surface_ = wl_compositor_create_surface(compositor_);
    if (!surface_)
    {
        fprintf(stderr, "[sparrow-app-runner] Failed to create wl_surface\n");
        return false;
    }

    wl_surface_add_listener(surface_, &surface_listener, this);

    // Create xdg_surface
    xdg_surface_ = xdg_wm_base_get_xdg_surface(xdg_wm_base_, surface_);
    if (!xdg_surface_)
    {
        fprintf(stderr, "[sparrow-app-runner] Failed to create xdg_surface\n");
        return false;
    }

    xdg_surface_add_listener(xdg_surface_, &xdg_surface_listener, this);

    // Create xdg_toplevel
    xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    if (!xdg_toplevel_)
    {
        fprintf(stderr, "[sparrow-app-runner] Failed to create xdg_toplevel\n");
        return false;
    }

    xdg_toplevel_add_listener(xdg_toplevel_, &xdg_toplevel_listener, this);

    xdg_toplevel_set_app_id(xdg_toplevel_, app_id.c_str());
    xdg_toplevel_set_title(xdg_toplevel_, title.c_str());

    if (fullscreen_)
    {
        xdg_toplevel_set_fullscreen(xdg_toplevel_, nullptr);
    } else if (maximized_)
    {
        xdg_toplevel_set_maximized(xdg_toplevel_);
    }

    // Initial commit to prompt compositor for configure event
    wl_surface_commit(surface_);

    // Initialize EGL (if requested)
    if (enable_egl_)
    {
        if (!init_egl())
        {
            fprintf(stderr, "[sparrow-app-runner] Failed to initialize EGL\n");
            return false;
        }
    }

    // Wait for initial configure roundtrip
    while (!configured_ && running_)
    {
        if (wl_display_dispatch(display_) == -1)
        {
            fprintf(stderr, "[sparrow-app-runner] Error during initial Wayland configure dispatch\n");
            return false;
        }
    }

    return true;
}

bool WaylandWindow::init_egl()
{
    egl_display_ = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(display_));
    if (egl_display_ == EGL_NO_DISPLAY)
    {
        fprintf(stderr, "[sparrow-app-runner] eglGetDisplay failed\n");
        return false;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(egl_display_, &major, &minor))
    {
        fprintf(stderr, "[sparrow-app-runner] eglInitialize failed\n");
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API))
    {
        fprintf(stderr, "[sparrow-app-runner] eglBindAPI(EGL_OPENGL_ES_API) failed\n");
        return false;
    }

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };

    EGLint num_configs = 0;
    if (!eglChooseConfig(egl_display_, config_attribs, &egl_config_, 1, &num_configs) || (num_configs < 1))
    {
        fprintf(stderr, "[sparrow-app-runner] eglChooseConfig failed to find a valid config\n");
        return false;
    }

    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT, context_attribs);
    if (egl_context_ == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "[sparrow-app-runner] eglCreateContext failed for main render context\n");
        return false;
    }

    egl_resource_context_ = eglCreateContext(egl_display_, egl_config_, egl_context_, context_attribs);
    if (egl_resource_context_ == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "[sparrow-app-runner] eglCreateContext failed for resource context\n");
        return false;
    }

    egl_window_ = wl_egl_window_create(surface_, width_, height_);
    if (!egl_window_)
    {
        fprintf(stderr, "[sparrow-app-runner] wl_egl_window_create failed\n");
        return false;
    }

    egl_surface_ = eglCreateWindowSurface(egl_display_, egl_config_,
        reinterpret_cast<EGLNativeWindowType>(egl_window_), nullptr);
    if (egl_surface_ == EGL_NO_SURFACE)
    {
        fprintf(stderr, "[sparrow-app-runner] eglCreateWindowSurface failed\n");
        return false;
    }

    return true;
}

void WaylandWindow::destroy_egl()
{
    if (egl_display_ != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_surface_ != EGL_NO_SURFACE)
        {
            eglDestroySurface(egl_display_, egl_surface_);
            egl_surface_ = EGL_NO_SURFACE;
        }

        if (egl_resource_context_ != EGL_NO_CONTEXT)
        {
            eglDestroyContext(egl_display_, egl_resource_context_);
            egl_resource_context_ = EGL_NO_CONTEXT;
        }

        if (egl_context_ != EGL_NO_CONTEXT)
        {
            eglDestroyContext(egl_display_, egl_context_);
            egl_context_ = EGL_NO_CONTEXT;
        }

        eglTerminate(egl_display_);
        egl_display_ = EGL_NO_DISPLAY;
    }

    if (egl_window_)
    {
        wl_egl_window_destroy(egl_window_);
        egl_window_ = nullptr;
    }
}

void WaylandWindow::request_close()
{
    running_ = false;
    wake();
    if (session_lock_mode_)
    {
        unlock_and_destroy();
    } else if (on_close_requested)
    {
        auto cb = std::move(on_close_requested);
        on_close_requested = nullptr;
        cb();
    }
}

void WaylandWindow::unlock_and_destroy()
{
    if (session_lock_)
    {
        printf("[sparrow-app-runner] Unlocking Wayland session...\n");
        fflush(stdout);

        for (auto & sec : secondary_lock_surfaces_)
        {
            if (sec.lock_surface)
            {
                ext_session_lock_surface_v1_destroy(sec.lock_surface);
                sec.lock_surface = nullptr;
            }

            if (sec.surface)
            {
                wl_surface_destroy(sec.surface);
                sec.surface = nullptr;
            }
        }

        secondary_lock_surfaces_.clear();

        if (lock_surface_)
        {
            ext_session_lock_surface_v1_destroy(lock_surface_);
            lock_surface_ = nullptr;
        }

        ext_session_lock_v1_unlock_and_destroy(session_lock_);
        session_lock_   = nullptr;
        session_locked_ = false;

        if (display_)
        {
            wl_display_roundtrip(display_);
        }
    }

    running_ = false;
    wake();
    if (on_close_requested)
    {
        auto cb = std::move(on_close_requested);
        on_close_requested = nullptr;
        cb();
    }
}

void WaylandWindow::shutdown()
{
    if (session_lock_)
    {
        unlock_and_destroy();
    }

    if (lock_manager_)
    {
        ext_session_lock_manager_v1_destroy(lock_manager_);
        lock_manager_ = nullptr;
    }

    for (auto *out : outputs_)
    {
        if (wl_output_get_version(out) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
        {
            wl_output_release(out);
        } else
        {
            wl_output_destroy(out);
        }
    }

    outputs_.clear();

    destroy_egl();

    if (xdg_toplevel_)
    {
        xdg_toplevel_destroy(xdg_toplevel_);
        xdg_toplevel_ = nullptr;
    }

    if (xdg_surface_)
    {
        xdg_surface_destroy(xdg_surface_);
        xdg_surface_ = nullptr;
    }

    if (security_)
    {
        zcr_security_v1_destroy(security_);
        security_ = nullptr;
    }

    if (secure_output_)
    {
        zcr_secure_output_v1_destroy(secure_output_);
        secure_output_ = nullptr;
    }

    if (surface_)
    {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }

    if (cursor_shape_manager_)
    {
        wp_cursor_shape_manager_v1_destroy(cursor_shape_manager_);
        cursor_shape_manager_ = nullptr;
    }

    if (ipc_manager_)
    {
        sparrow_ipc_manager_v1_destroy(ipc_manager_);
        ipc_manager_ = nullptr;
    }

    if (ipc_fd_ >= 0)
    {
        ::close(ipc_fd_);
        ipc_fd_ = -1;
    }

    if (wake_fd_ >= 0)
    {
        ::close(wake_fd_);
        wake_fd_ = -1;
    }

    if (xdg_wm_base_)
    {
        xdg_wm_base_destroy(xdg_wm_base_);
        xdg_wm_base_ = nullptr;
    }

    if (current_data_source_)
    {
        wl_data_source_destroy(current_data_source_);
        current_data_source_ = nullptr;
    }

    if (current_data_offer_)
    {
        wl_data_offer_destroy(current_data_offer_);
        current_data_offer_ = nullptr;
    }

    if (pending_offer_)
    {
        wl_data_offer_destroy(pending_offer_);
        pending_offer_ = nullptr;
    }

    if (data_device_)
    {
        if (wl_data_device_get_version(data_device_) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION)
        {
            wl_data_device_release(data_device_);
        } else
        {
            wl_data_device_destroy(data_device_);
        }

        data_device_ = nullptr;
    }

    if (data_device_manager_)
    {
        wl_data_device_manager_destroy(data_device_manager_);
        data_device_manager_ = nullptr;
    }

    if (seat_)
    {
        wl_seat_destroy(seat_);
        seat_ = nullptr;
    }

    if (shm_)
    {
        wl_shm_destroy(shm_);
        shm_ = nullptr;
    }

    if (subcompositor_)
    {
        wl_subcompositor_destroy(subcompositor_);
        subcompositor_ = nullptr;
    }

    if (compositor_)
    {
        wl_compositor_destroy(compositor_);
        compositor_ = nullptr;
    }

    if (registry_)
    {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }

    if (display_)
    {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }

    running_ = false;
}

void WaylandWindow::set_repeat_timer(int fd, std::function<void()> on_timer)
{
    repeat_timer_fd_ = fd;
    on_repeat_timer_ = std::move(on_timer);
}

void WaylandWindow::wake()
{
    if (wake_fd_ >= 0)
    {
        uint64_t val = 1;
        ssize_t ret  = write(wake_fd_, &val, sizeof(val));
        (void)ret;
    }
}

void WaylandWindow::dispatch_events(int timeout_ms)
{
    if (!display_ || !running_)
    {
        return;
    }

    while (wl_display_prepare_read(display_) != 0)
    {
        if (wl_display_dispatch_pending(display_) < 0)
        {
            running_ = false;
            return;
        }
    }

    wl_display_flush(display_);

    int wayland_fd = wl_display_get_fd(display_);
    struct pollfd pfds[4];
    int wayland_idx = 0;
    pfds[0].fd     = wayland_fd;
    pfds[0].events = POLLIN;
    int num_fds = 1;

    int ipc_idx = -1;
    int ipc_fd  = ipc_client_ ? ipc_client_->get_fd() : -1;
    if (ipc_fd >= 0)
    {
        ipc_idx = num_fds;
        pfds[ipc_idx].fd     = ipc_fd;
        pfds[ipc_idx].events = POLLIN;
        num_fds++;
    }

    int timer_idx = -1;
    if (repeat_timer_fd_ >= 0)
    {
        timer_idx = num_fds;
        pfds[timer_idx].fd     = repeat_timer_fd_;
        pfds[timer_idx].events = POLLIN;
        num_fds++;
    }

    int wake_idx = -1;
    if (wake_fd_ >= 0)
    {
        wake_idx = num_fds;
        pfds[wake_idx].fd     = wake_fd_;
        pfds[wake_idx].events = POLLIN;
        num_fds++;
    }

    int ret = poll(pfds, num_fds, timeout_ms);

    if (ret > 0)
    {
        if ((wake_idx >= 0) && (pfds[wake_idx].revents & POLLIN))
        {
            uint64_t val = 0;
            ssize_t r    = read(wake_fd_, &val, sizeof(val));
            (void)r;
        }

        if ((ipc_idx >= 0) && (pfds[ipc_idx].revents & POLLIN))
        {
            ipc_client_->dispatch_read();
        }

        if ((timer_idx >= 0) && (pfds[timer_idx].revents & POLLIN))
        {
            if (on_repeat_timer_)
            {
                on_repeat_timer_();
            }
        }

        if (pfds[wayland_idx].revents & POLLIN)
        {
            wl_display_read_events(display_);
            if (wl_display_dispatch_pending(display_) < 0)
            {
                running_ = false;
            }
        } else
        {
            wl_display_cancel_read(display_);
        }
    } else if (ret < 0)
    {
        wl_display_cancel_read(display_);
        if (errno != EINTR)
        {
            running_ = false;
        }
    } else
    {
        wl_display_cancel_read(display_);
    }
}

bool WaylandWindow::make_current()
{
    if ((egl_display_ == EGL_NO_DISPLAY) || (egl_surface_ == EGL_NO_SURFACE) ||
        (egl_context_ == EGL_NO_CONTEXT))
    {
        return false;
    }

    return eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_) == EGL_TRUE;
}

bool WaylandWindow::clear_current()
{
    if (egl_display_ == EGL_NO_DISPLAY)
    {
        return false;
    }

    return eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_TRUE;
}

bool WaylandWindow::make_resource_current()
{
    if ((egl_display_ == EGL_NO_DISPLAY) || (egl_resource_context_ == EGL_NO_CONTEXT))
    {
        return false;
    }

    return eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_resource_context_) == EGL_TRUE;
}

bool WaylandWindow::swap_buffers()
{
    if ((egl_display_ == EGL_NO_DISPLAY) || (egl_surface_ == EGL_NO_SURFACE))
    {
        return false;
    }

    return eglSwapBuffers(egl_display_, egl_surface_) == EGL_TRUE;
}

void* WaylandWindow::gl_proc_resolver(void *user_data, const char *name)
{
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

void WaylandWindow::handle_registry_global(struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
    if (strcmp(interface, wl_compositor_interface.name) == 0)
    {
        compositor_ = static_cast<struct wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min<uint32_t>(version, 4)));
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
    {
        subcompositor_ = static_cast<struct wl_subcompositor*>(
            wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    } else if (strcmp(interface, wl_shm_interface.name) == 0)
    {
        shm_ = static_cast<struct wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, wl_seat_interface.name) == 0)
    {
        seat_ = static_cast<struct wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min<uint32_t>(version, 7)));
        if (on_seat_bound)
        {
            on_seat_bound(seat_);
        }

        if (data_device_manager_ && !data_device_)
        {
            bind_data_device();
        }
    } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0)
    {
        data_device_manager_ = static_cast<struct wl_data_device_manager*>(
            wl_registry_bind(registry, name, &wl_data_device_manager_interface,
                             std::min<uint32_t>(version, 3)));
        if (seat_ && !data_device_)
        {
            bind_data_device();
        }
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        xdg_wm_base_ = static_cast<struct xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(xdg_wm_base_, &xdg_wm_base_listener, this);
    } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0)
    {
        cursor_shape_manager_ = static_cast<struct wp_cursor_shape_manager_v1*>(
            wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1));
    } else if (strcmp(interface, sparrow_ipc_manager_v1_interface.name) == 0)
    {
        ipc_manager_ = static_cast<struct sparrow_ipc_manager_v1*>(
            wl_registry_bind(registry, name, &sparrow_ipc_manager_v1_interface, 1));
        sparrow_ipc_manager_v1_add_listener(ipc_manager_, &ipc_manager_listener, this);
        printf(
            "[sparrow-app-runner] Bound sparrow_ipc_manager_v1; requesting secure socketpair channel...\n");
        fflush(stdout);
        sparrow_ipc_manager_v1_get_ipc_channel(ipc_manager_, app_id_.c_str());
    } else if (strcmp(interface, "wl_output") == 0)
    {
        auto *out = static_cast<struct wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min<uint32_t>(version, 4)));
        outputs_.push_back(out);
    } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0)
    {
        lock_manager_ = static_cast<struct ext_session_lock_manager_v1*>(
            wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1));
        printf("[sparrow-app-runner] Bound ext_session_lock_manager_v1\n");
        fflush(stdout);
    } else if (strcmp(interface, zcr_secure_output_v1_interface.name) == 0)
    {
        secure_output_ = static_cast<struct zcr_secure_output_v1*>(
            wl_registry_bind(registry, name, &zcr_secure_output_v1_interface, 1));
        printf("[sparrow-app-runner] Bound zcr_secure_output_v1\n");
        fflush(stdout);
    }
}

void WaylandWindow::handle_registry_global_remove(struct wl_registry *registry, uint32_t name)
{
    (void)registry;
    (void)name;
}

void WaylandWindow::handle_xdg_surface_configure(struct xdg_surface *surface, uint32_t serial)
{
    xdg_surface_ack_configure(surface, serial);
    configured_ = true;

    if (on_window_metrics_changed)
    {
        on_window_metrics_changed(width_, height_, pixel_ratio_);
    }
}

void WaylandWindow::handle_xdg_toplevel_configure(struct xdg_toplevel *toplevel,
    int32_t width,
    int32_t height,
    struct wl_array *states)
{
    (void)toplevel;

    if (states && (states->data != nullptr))
    {
        maximized_  = false;
        fullscreen_ = false;
        bool activated = false;
        const auto *states_data = static_cast<const uint32_t*>(states->data);
        size_t count = states->size / sizeof(uint32_t);
        for (size_t i = 0; i < count; ++i)
        {
            if (states_data[i] == XDG_TOPLEVEL_STATE_MAXIMIZED)
            {
                maximized_ = true;
            } else if (states_data[i] == XDG_TOPLEVEL_STATE_FULLSCREEN)
            {
                fullscreen_ = true;
            } else if (states_data[i] == XDG_TOPLEVEL_STATE_ACTIVATED)
            {
                activated = true;
            }
        }

        if (activated != activated_)
        {
            activated_ = activated;
            if (on_window_activated)
            {
                on_window_activated(activated_);
            }
        }
    }

    if ((width > 0) && (height > 0))
    {
        if ((width_ != width) || (height_ != height))
        {
            width_  = width;
            height_ = height;

            if (egl_window_)
            {
                wl_egl_window_resize(egl_window_, width_, height_, 0, 0);
            }

            if (on_window_resized)
            {
                on_window_resized(width_, height_);
            }

            if (configured_ && on_window_metrics_changed)
            {
                on_window_metrics_changed(width_, height_, pixel_ratio_);
            }
        }
    }
}

void WaylandWindow::set_title(const std::string & title)
{
    title_ = title;
    if (xdg_toplevel_)
    {
        printf("[sparrow-app-runner] Wayland -> Sparrow: xdg_toplevel_set_title(\"%s\")\n", title_.c_str());
        fflush(stdout);
        xdg_toplevel_set_title(xdg_toplevel_, title_.c_str());
        if (surface_)
        {
            wl_surface_commit(surface_);
        }

        if (display_)
        {
            wl_display_flush(display_);
        }
    }
}

void WaylandWindow::set_app_id(const std::string & app_id)
{
    app_id_ = app_id;
    if (xdg_toplevel_)
    {
        printf("[sparrow-app-runner] Wayland -> Sparrow: xdg_toplevel_set_app_id(\"%s\")\n", app_id_.c_str());
        fflush(stdout);
        xdg_toplevel_set_app_id(xdg_toplevel_, app_id_.c_str());
        if (surface_)
        {
            wl_surface_commit(surface_);
        }

        if (display_)
        {
            wl_display_flush(display_);
        }
    }
}

void WaylandWindow::set_fullscreen(bool fullscreen)
{
    fullscreen_ = fullscreen;
    if (xdg_toplevel_)
    {
        printf("[sparrow-app-runner] Wayland -> Sparrow: xdg_toplevel_%s_fullscreen()\n",
            fullscreen ? "set" : "unset");
        fflush(stdout);
        if (fullscreen)
        {
            xdg_toplevel_set_fullscreen(xdg_toplevel_, nullptr);
        } else
        {
            xdg_toplevel_unset_fullscreen(xdg_toplevel_);
        }

        if (surface_)
        {
            wl_surface_commit(surface_);
        }

        if (display_)
        {
            wl_display_flush(display_);
        }
    }
}

void WaylandWindow::set_maximized(bool maximized)
{
    maximized_ = maximized;
    if (xdg_toplevel_)
    {
        printf("[sparrow-app-runner] Wayland -> Sparrow: xdg_toplevel_%s_maximized()\n",
            maximized ? "set" : "unset");
        fflush(stdout);
        if (maximized)
        {
            xdg_toplevel_set_maximized(xdg_toplevel_);
        } else
        {
            xdg_toplevel_unset_maximized(xdg_toplevel_);
        }

        if (surface_)
        {
            wl_surface_commit(surface_);
        }

        if (display_)
        {
            wl_display_flush(display_);
        }
    }
}

void WaylandWindow::minimize()
{
    if (xdg_toplevel_)
    {
        printf("[sparrow-app-runner] Wayland -> Sparrow: xdg_toplevel_set_minimized()\n");
        fflush(stdout);
        xdg_toplevel_set_minimized(xdg_toplevel_);
        if (surface_)
        {
            wl_surface_commit(surface_);
        }

        if (display_)
        {
            wl_display_flush(display_);
        }
    }
}

void WaylandWindow::set_content_sensitivity(bool sensitive)
{
    if (content_sensitive_ == sensitive)
    {
        return;
    }

    content_sensitive_ = sensitive;
    printf("[sparrow-app-runner] Setting surface content sensitivity: %s\n",
        sensitive ? "SENSITIVE" : "NOT_SENSITIVE");
    fflush(stdout);

    if (secure_output_ && surface_)
    {
        if (sensitive)
        {
            if (!security_)
            {
                security_ = zcr_secure_output_v1_get_security(
                    secure_output_, surface_);
                if (security_)
                {
                    zcr_security_v1_only_visible_on_secure_output(security_);
                }
            }
        } else if (security_)
        {
            zcr_security_v1_destroy(security_);
            security_ = nullptr;
        }

        wl_surface_commit(surface_);
        if (display_)
        {
            wl_display_flush(display_);
        }
    }

    if (ipc_client_ && ipc_client_->is_connected())
    {
        std::string params = std::string("{\"sensitive\":") +
            (sensitive ? "true" : "false") + "}";
        ipc_client_->send_request("setSurfaceSensitive", params,
            [] (bool, const simdjson::dom::element &) {});
    }
}

void WaylandWindow::handle_xdg_toplevel_close(struct xdg_toplevel *toplevel)
{
    (void)toplevel;
    running_ = false;
    if (on_close_requested)
    {
        on_close_requested();
    }
}

void WaylandWindow::handle_surface_enter(struct wl_surface *surface, struct wl_output *output)
{
    (void)surface;
    (void)output;
}

void WaylandWindow::handle_surface_leave(struct wl_surface *surface, struct wl_output *output)
{
    (void)surface;
    (void)output;
}

void WaylandWindow::handle_session_lock_locked()
{
    printf("[sparrow-app-runner] ext-session-lock-v1: Session locked event confirmed by compositor!\n");
    fflush(stdout);
    session_locked_ = true;
    if (on_session_locked)
    {
        on_session_locked(true);
    }
}

void WaylandWindow::handle_session_lock_finished()
{
    fprintf(stderr, "[sparrow-app-runner] ext-session-lock-v1: Lock finished/rejected by compositor\n");
    session_locked_ = false;
    running_ = false;
    if (on_session_locked)
    {
        on_session_locked(false);
    }
}

void WaylandWindow::handle_session_lock_surface_configure(
    struct ext_session_lock_surface_v1 *surface,
    uint32_t serial, uint32_t width, uint32_t height)
{
    ext_session_lock_surface_v1_ack_configure(surface, serial);
    width_  = static_cast<int32_t>(width);
    height_ = static_cast<int32_t>(height);
    configured_ = true;

    if (egl_window_)
    {
        wl_egl_window_resize(egl_window_, width_, height_, 0, 0);
    }

    if (on_window_metrics_changed)
    {
        on_window_metrics_changed(width_, height_, pixel_ratio_);
    }

    if (on_window_resized)
    {
        on_window_resized(width_, height_);
    }
}

void WaylandWindow::handle_secondary_lock_surface_configure(
    SecondaryLockSurface *sec_surf,
    uint32_t serial, uint32_t width, uint32_t height)
{
    if (!sec_surf || !sec_surf->surface || !sec_surf->lock_surface)
    {
        return;
    }

    ext_session_lock_surface_v1_ack_configure(sec_surf->lock_surface, serial);
    struct wl_buffer *buf = create_black_shm_buffer(shm_, width, height);
    if (buf)
    {
        wl_surface_attach(sec_surf->surface, buf, 0, 0);
    }

    wl_surface_commit(sec_surf->surface);
}

void WaylandWindow::handle_ipc_channel(int fd)
{
    ipc_fd_ = fd;
    printf("[sparrow-app-runner] Received secure IPC socketpair fd=%d from compositor\n", fd);
    fflush(stdout);

    if (ipc_client_)
    {
        ipc_client_->set_fd(fd);
    }

    if (on_ipc_fd_received)
    {
        on_ipc_fd_received(fd);
    }
}

void WaylandWindow::set_ipc_client(class IpcClient *client)
{
    ipc_client_ = client;
    if (ipc_client_ && (ipc_fd_ >= 0))
    {
        ipc_client_->set_fd(ipc_fd_);
    }
}

void WaylandWindow::bind_data_device()
{
    if (data_device_manager_ && seat_ && !data_device_)
    {
        data_device_ = wl_data_device_manager_get_data_device(data_device_manager_, seat_);
        if (data_device_)
        {
            wl_data_device_add_listener(data_device_, &data_device_listener, this);
        }
    }
}

void WaylandWindow::handle_data_offer(struct wl_data_offer *offer)
{
    if (pending_offer_ && (pending_offer_ != current_data_offer_))
    {
        wl_data_offer_destroy(pending_offer_);
    }

    pending_offer_ = offer;
    pending_mimes_.clear();
    if (offer)
    {
        wl_data_offer_add_listener(offer, &data_offer_listener, this);
    }
}

void WaylandWindow::handle_offer_mime_type(struct wl_data_offer *offer, const char *mime_type)
{
    if (offer && mime_type)
    {
        if (offer == pending_offer_)
        {
            pending_mimes_.push_back(mime_type);
        } else if (offer == current_data_offer_)
        {
            current_offer_mime_types_.push_back(mime_type);
        }
    }
}

void WaylandWindow::handle_selection(struct wl_data_offer *offer)
{
    if (current_data_offer_ && (current_data_offer_ != offer))
    {
        wl_data_offer_destroy(current_data_offer_);
    }

    current_data_offer_ = offer;
    if (offer && (offer == pending_offer_))
    {
        current_offer_mime_types_ = std::move(pending_mimes_);
        pending_offer_ = nullptr;
    } else if (!offer)
    {
        current_offer_mime_types_.clear();
        if (pending_offer_)
        {
            wl_data_offer_destroy(pending_offer_);
            pending_offer_ = nullptr;
        }
    }
}

void WaylandWindow::handle_data_source_send(struct wl_data_source *source, const char *mime_type, int32_t fd)
{
    (void)source;
    (void)mime_type;
    if (fd >= 0)
    {
        size_t written   = 0;
        const char *data = clipboard_text_.data();
        size_t total     = clipboard_text_.size();
        while (written < total)
        {
            ssize_t ret = write(fd, data + written, total - written);
            if (ret <= 0)
            {
                break;
            }

            written += static_cast<size_t>(ret);
        }

        close(fd);
    }
}

void WaylandWindow::handle_data_source_cancelled(struct wl_data_source *source)
{
    if (current_data_source_ == source)
    {
        current_data_source_ = nullptr;
    }

    wl_data_source_destroy(source);
}

void WaylandWindow::set_clipboard_text(const std::string & text)
{
    clipboard_text_ = text;

    if (!data_device_manager_ || !data_device_)
    {
        return;
    }

    if (current_data_source_)
    {
        wl_data_source_destroy(current_data_source_);
        current_data_source_ = nullptr;
    }

    current_data_source_ = wl_data_device_manager_create_data_source(data_device_manager_);
    if (!current_data_source_)
    {
        return;
    }

    wl_data_source_add_listener(current_data_source_, &data_source_listener, this);
    wl_data_source_offer(current_data_source_, "text/plain;charset=utf-8");
    wl_data_source_offer(current_data_source_, "text/plain");
    wl_data_source_offer(current_data_source_, "UTF8_STRING");
    wl_data_source_offer(current_data_source_, "STRING");
    wl_data_source_offer(current_data_source_, "TEXT");

    wl_data_device_set_selection(data_device_, current_data_source_, last_serial_);
    wl_display_flush(display_);
}

std::string WaylandWindow::get_clipboard_text()
{
    // If we own the current selection, return local cache directly
    if (current_data_source_ != nullptr)
    {
        return clipboard_text_;
    }

    // If an external Wayland offer is present, read from it
    if (current_data_offer_ != nullptr)
    {
        const char *preferred_mime = nullptr;
        const char *mimes[] = {
            "text/plain;charset=utf-8",
            "text/plain",
            "UTF8_STRING",
            "STRING",
            "TEXT",
        };
        for (const char *m : mimes)
        {
            for (const auto & offered : current_offer_mime_types_)
            {
                if (offered == m)
                {
                    preferred_mime = m;
                    break;
                }
            }

            if (preferred_mime)
            {
                break;
            }
        }

        if (preferred_mime)
        {
            int fds[2];
            if (pipe2(fds, O_CLOEXEC) == 0)
            {
                wl_data_offer_receive(current_data_offer_, preferred_mime, fds[1]);
                close(fds[1]); // Close write end in our process so EOF is signaled
                wl_display_flush(display_);

                std::string received;
                struct pollfd pfd = {};
                pfd.fd     = fds[0];
                pfd.events = POLLIN;

                // Wait up to 250ms for remote client response
                while (poll(&pfd, 1, 250) > 0)
                {
                    char buf[4096];
                    ssize_t n = read(fds[0], buf, sizeof(buf));
                    if (n <= 0)
                    {
                        break;
                    }

                    received.append(buf, n);
                }

                close(fds[0]);

                if (!received.empty())
                {
                    clipboard_text_ = received;
                    return clipboard_text_;
                }
            }
        }
    }

    return clipboard_text_;
}

bool WaylandWindow::has_clipboard_text()
{
    if (current_data_source_ != nullptr)
    {
        return !clipboard_text_.empty();
    }

    if ((current_data_offer_ != nullptr) && !current_offer_mime_types_.empty())
    {
        return true;
    }

    return !clipboard_text_.empty();
}
