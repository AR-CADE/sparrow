#include <assert.h>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <optional>
#include <signal.h>
#include <sys/resource.h>

#include "core.hpp"

#include <sparrow/nonstd/wlroots-full.hpp>
#include <sparrow/options.hpp>

#include "flutter/platform/cursor.hpp"
#include "flutter/platform/engine.hpp"
#include "flutter/platform/isolate.hpp"
#include "flutter/platform/keyboard_channel.hpp"
#include "flutter/platform/task.hpp"
#include "flutter/platform/text_input.hpp"
#include "input/keyboard.hpp"
#include "input/pointer.hpp"
#include "input/seat.hpp"
#include "output.hpp"
#include "surface/decoration.hpp"
#include "surface/popup.hpp"
#include "surface/sub_surface.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"
#include "util/dispatcher.hpp"
#include "util/realtime.hpp"
#include "util/udmabuf.hpp"
#include "ipc/ipc_server.hpp"
#if defined (SPARROW_ENABLE_TRACE)
    #include "util/trace.hpp"
#endif

struct rlimit user_maxfiles;

struct wlr_fixes
{
    struct wl_global *global;

    struct
    {
        struct wl_signal destroy;
    } events;

    struct
    {
        struct wl_listener display_destroy;
    };
};

#define FIXES_VERSION 1

static void fixes_destroy(struct wl_client *client,
    struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void fixes_destroy_registry(struct wl_client *client,
    struct wl_resource *resource,
    struct wl_resource *registry)
{
    wl_resource_destroy(registry);
}

static const struct wl_fixes_interface fixes_impl = {
    .destroy = fixes_destroy,
    .destroy_registry = fixes_destroy_registry,
};

static void fixes_bind(struct wl_client *wl_client, void *data,
    uint32_t version, uint32_t id)
{
    struct wlr_fixes *fixes = static_cast<struct wlr_fixes*>(data);

    struct wl_resource *resource =
        wl_resource_create(wl_client, &wl_fixes_interface, version, id);
    if (resource == nullptr)
    {
        wl_client_post_no_memory(wl_client);
        return;
    }

    wl_resource_set_implementation(resource, &fixes_impl, fixes, nullptr);
}

static void fixes_handle_display_destroy(struct wl_listener *listener,
    void *data)
{
    struct wlr_fixes *fixes = wl_container_of(listener, fixes, display_destroy);
    wl_signal_emit_mutable(&fixes->events.destroy, nullptr);

    assert(wl_list_empty(&fixes->events.destroy.listener_list));

    wl_list_remove(&fixes->display_destroy.link);
    wl_global_destroy(fixes->global);
    delete fixes;
}

struct wlr_fixes *wlr_fixes_create(struct wl_display *display,
    uint32_t version)
{
    assert(version <= FIXES_VERSION);

    struct wlr_fixes *fixes = new wlr_fixes();
    if (fixes == nullptr)
    {
        return nullptr;
    }

    fixes->global = wl_global_create(display, &wl_fixes_interface, version, fixes,
        fixes_bind);
    if (fixes->global == nullptr)
    {
        delete fixes;
        return nullptr;
    }

    wl_signal_init(&fixes->events.destroy);

    fixes->display_destroy.notify = fixes_handle_display_destroy;
    wl_display_add_destroy_listener(display, &fixes->display_destroy);

    return fixes;
}

static void increase_nofile_limit()
{
    if (getrlimit(RLIMIT_NOFILE, &user_maxfiles) != 0)
    {
        // wlr_log( WLR_ERROR ,"Failed to getrlimit(RLIMIT_NOFILE), not increasing
        // maximum open file descriptors. Might cause"
        // " crashes with many open views.");
    } else
    {
        struct rlimit max_files = user_maxfiles;
        max_files.rlim_cur = user_maxfiles.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &max_files) != 0)
        {
            // wlr_log( WLR_ERROR ,"Failed to setrlimit(RLIMIT_NOFILE), not
            // increasing maximum open file descriptors. Might "
            // "cause crashes with many open views.");
        }
    }
}

static std::string get_version_string()
{
    return std::string(SPARROW_VERSION) + "-" + " wlroots-" + WLR_VERSION_STR;
}

static bool drop_permissions(void)
{
    if ((getuid() != geteuid()) || (getgid() != getegid()))
    {
        // Set the gid and uid in the correct order.
        if ((setgid(getgid()) != 0) || (setuid(getuid()) != 0))
        {
            wlr_log(WLR_ERROR, "Unable to drop root, refusing to start");

            return false;
        }
    }

    if ((setgid(0) != -1) || (setuid(0) != -1))
    {
        wlr_log(WLR_ERROR, "Unable to drop root (we shouldn't be able to "
                           "restore it after setuid), refusing to start");

        return false;
    }

    return true;
}

extern std::optional<int> exit_because_signal;

static int handle_term_signal(int signal, void *data)
{
    Core *instance = static_cast<Core*>(data);
    exit_because_signal = signal;
    if (instance && instance->wl_display)
    {
        wl_display_terminate(instance->wl_display);
    }

    return 0;
}

static std::optional<std::string> choose_socket(wl_display *display)
{
    for (int i = 1; i <= 32; i++)
    {
        auto name = "wayland-" + std::to_string(i);
        if (wl_display_add_socket(display, name.c_str()) >= 0)
        {
            return name;
        }
    }

    return {};
}

std::atomic<Core*> Core::_instance{nullptr};
std::mutex Core::m_;

Core*Core::instance()
{
    Core *instance = _instance.load(std::memory_order_relaxed);
    if (instance == nullptr)
    {
        std::lock_guard<std::mutex> lock(m_);
        if (_instance == nullptr)
        {
            instance = new Core();
            _instance.store(instance, std::memory_order_release);
        }
    }

    return instance;
}

Core::~Core()
{
    wlr_log(WLR_INFO, "destroy core:%d", _instance != nullptr);

    if (xdg_activation_request_activate.link.next &&
        xdg_activation_request_activate.link.prev &&
        (xdg_activation_request_activate.link.next != &xdg_activation_request_activate.link))
    {
        wl_list_remove(&xdg_activation_request_activate.link);
        wl_list_init(&xdg_activation_request_activate.link);
    }

    if (subsurfaces)
    {
        handle_map_destroy(subsurfaces);
        subsurfaces = nullptr;
    }

    if (popups)
    {
        handle_map_destroy(popups);
        popups = nullptr;
    }

    if (fps_decay_timer != nullptr)
    {
        wl_event_source_remove(fps_decay_timer);
        fps_decay_timer = nullptr;
    }

    if (ipc_server)
    {
        delete ipc_server;
        ipc_server = nullptr;
    }

#if defined (SPARROW_ENABLE_TRACE)
    SparrowTrace::instance().finish();
#endif
}

void Core::record_client_commit(uint64_t now_us)
{
    if (!show_fps)
    {
        return;
    }

    client_commit_timestamps[client_commit_head] = now_us;
    client_commit_head = (client_commit_head + 1) % MAX_COMMIT_HISTORY;
    if (client_commit_count < MAX_COMMIT_HISTORY)
    {
        client_commit_count++;
    }

    if (fps_decay_timer != nullptr)
    {
        wl_event_source_timer_update(fps_decay_timer, 150);
    }
}

SparrowView*Core::find_view_by_handle(uint32_t handle)
{
    if ((handle == 0) || wl_list_empty(&views_list))
    {
        return nullptr;
    }

    SparrowView *v = nullptr;
    wl_list_for_each(v, &views_list, link)
    {
        if (v->handle == handle)
        {
            return v;
        }
    }
    return nullptr;
}

SparrowView*Core::find_view_by_toplevel(const struct wlr_xdg_toplevel *toplevel)
{
    if (!toplevel || wl_list_empty(&views_list))
    {
        return nullptr;
    }

    SparrowView *v = nullptr;
    wl_list_for_each(v, &views_list, link)
    {
        if (v->toplevel == toplevel)
        {
            return v;
        }
    }
    return nullptr;
}

SparrowView*Core::find_view_by_wlr_surface(const struct wlr_surface *surface)
{
    if (!surface || wl_list_empty(&views_list))
    {
        return nullptr;
    }

    SparrowView *v = nullptr;
    wl_list_for_each(v, &views_list, link)
    {
        if (v->xdg_surface && (v->xdg_surface->surface == surface))
        {
            return v;
        }
    }
    return nullptr;
}

SparrowView*Core::find_view_by_xdg_surface(const struct wlr_xdg_surface *xdg_surface)
{
    if (!xdg_surface || wl_list_empty(&views_list))
    {
        return nullptr;
    }

    SparrowView *v = nullptr;
    wl_list_for_each(v, &views_list, link)
    {
        if (v->xdg_surface == xdg_surface)
        {
            return v;
        }
    }
    return nullptr;
}

void Core::dump_surface_tree() const
{
    fprintf(stderr,
        "\n\033[1;36m======================= SPARROW SURFACE TREE =======================\033[0m\n");

    // Outputs
    Output *out = nullptr;
    wl_list_for_each(out, const_cast<struct wl_list*>(&outputs), link)
    {
        if (!out->wlr_output)
        {
            continue;
        }

        int refresh = out->wlr_output->current_mode ? (out->wlr_output->current_mode->refresh / 1000) : 60;
        struct wlr_box out_box = {0, 0, 0, 0};
        if (output_layout)
        {
            wlr_output_layout_get_box(output_layout, out->wlr_output, &out_box);
        }

        fprintf(stderr,
            "\033[1;32m[Output]\033[0m %s (id=%d) [%dx%d @ %dHz] Scale: %.2f | Pos: (%d, %d) | %s\n",
            out->wlr_output->name, out->id,
            out->wlr_output->width, out->wlr_output->height, refresh,
            out->wlr_output->scale, out_box.x, out_box.y,
            (out == vsync_output) ? "VSync Primary" : "Secondary");
    }

    // Views / Toplevels
    int view_count    = 0;
    SparrowView *view = nullptr;
    wl_list_for_each(view, const_cast<struct wl_list*>(&views_list), link)
    {
        view_count++;
        pid_t pid = 0;
        uid_t uid = 0;
        gid_t gid = 0;
        if (view->xdg_surface && view->xdg_surface->resource)
        {
            struct wl_client *client = wl_resource_get_client(view->xdg_surface->resource);
            if (client)
            {
                wl_client_get_credentials(client, &pid, &uid, &gid);
            }
        }

        const char *app_id = (view->xdg_surface && view->xdg_surface->toplevel &&
            view->xdg_surface->toplevel->app_id) ?
            view->xdg_surface->toplevel->app_id :
            "(none)";
        const char *title = (view->xdg_surface && view->xdg_surface->toplevel &&
            view->xdg_surface->toplevel->title) ?
            view->xdg_surface->toplevel->title :
            "(untitled)";

        const char *buffer_type = "None";
        int buf_w = 0, buf_h = 0;
        if (view->xdg_surface && view->xdg_surface->surface)
        {
            buf_w = view->xdg_surface->surface->current.buffer_width;
            buf_h = view->xdg_surface->surface->current.buffer_height;
            if (view->xdg_surface->surface->buffer)
            {
                buffer_type =
                    view->xdg_surface->surface->buffer->texture ? "Hardware/Texture" : "ClientBuffer";
            }
        }

        fprintf(stderr, " \033[1;34m├─ [View #%d]\033[0m PID: %d | App: \"%s\" | Title: \"%s\"\n",
            view->handle, pid, app_id, title);
        fprintf(stderr, " │  ├─ Geometry: %dx%d @ (%d, %d) | Buffer: %s (%dx%d) | Focused: %s | Mapped: %s\n",
            view->width, view->height, view->x, view->y,
            buffer_type, buf_w, buf_h,
            view->activated ? "YES" : "NO",
            (view->xdg_surface && view->xdg_surface->surface &&
                view->xdg_surface->surface->mapped) ? "YES" : "NO");

        // Subsurfaces of this view
        if (subsurfaces)
        {
            subsurfaces->for_each([view] (uint32_t handle, void *val)
            {
                (void)handle;
                SparrowSubSurface *sub = static_cast<SparrowSubSurface*>(val);
                if (sub && (sub->parent_view == view))
                {
                    const char *sub_buf_type = "None";
                    if (sub->surface && sub->surface->buffer)
                    {
                        sub_buf_type = sub->surface->buffer->texture ? "Hardware/Texture" : "ClientBuffer";
                    }

                    fprintf(stderr,
                        " │  ├─ \033[1;33m[Sub #%d]\033[0m %dx%d @ (%d, %d) | Buffer: %s (%dx%d) | TexID: %ld\n",
                        sub->handle, sub->width, sub->height, sub->x, sub->y,
                        sub_buf_type, sub->buffer_width, sub->buffer_height, sub->texture_id);
                }
            });
        }

        // Popups of this view
        if (popups)
        {
            popups->for_each([view] (uint32_t handle, void *val)
            {
                (void)handle;
                SparrowPopup *pop = static_cast<SparrowPopup*>(val);
                if (pop && (pop->parent_view == view))
                {
                    fprintf(stderr,
                        " │  └─ \033[1;35m[Popup #%d]\033[0m %dx%d @ (%d, %d) | Mapped: %s | Unconstrained: %s\n",
                        pop->handle, pop->width, pop->height, pop->x, pop->y,
                        (pop->xdg_surface && pop->xdg_surface->surface &&
                            pop->xdg_surface->surface->mapped) ? "YES" : "NO",
                        pop->unconstrained ? "YES" : "NO");
                }
            });
        }
    }

    if (view_count == 0)
    {
        fprintf(stderr, " \033[0;90m(No active toplevel views)\033[0m\n");
    }

    fprintf(stderr,
        "\033[1;36m====================================================================\033[0m\n\n");
}

int Core::init(const sparrow_options & opts, bool allow_root)
{
    Core *core = this;

    const char *dbg_proto_env = getenv("SPARROW_DEBUG_PROTOCOL");
    if (dbg_proto_env != nullptr)
    {
        core->debug_protocol = (strcmp(dbg_proto_env, "1") == 0 ||
            strcasecmp(dbg_proto_env, "true") == 0);
    }

    const char *dbg_damage_env = getenv("SPARROW_DEBUG_DAMAGE");
    if (dbg_damage_env != nullptr)
    {
        core->debug_damage = (strcmp(dbg_damage_env, "1") == 0 ||
            strcasecmp(dbg_damage_env, "true") == 0);
    }

    const char *show_fps_env = getenv("SPARROW_SHOW_FPS");
    if (show_fps_env != nullptr)
    {
        core->show_fps = (strcmp(show_fps_env, "1") == 0 ||
            strcasecmp(show_fps_env, "true") == 0);
    }

    const char *dbg_pacing_env = getenv("SPARROW_DEBUG_PACING");
    if (dbg_pacing_env != nullptr)
    {
        core->debug_pacing = (strcmp(dbg_pacing_env, "1") == 0 ||
            strcasecmp(dbg_pacing_env, "true") == 0);
    }

    const char *buffering_env = getenv("SPARROW_BUFFERING");
    if (buffering_env != nullptr)
    {
        if ((strcasecmp(buffering_env, "double") == 0) || (strcmp(buffering_env, "0") == 0))
        {
            core->buffering_mode = Core::BUFFERING_DOUBLE;
        } else if ((strcasecmp(buffering_env, "triple") == 0) || (strcmp(buffering_env,
            "2") == 0) || (strcasecmp(buffering_env, "on") == 0))
        {
            core->buffering_mode = Core::BUFFERING_TRIPLE;
        } else if ((strcasecmp(buffering_env, "auto") == 0) || (strcmp(buffering_env,
            "1") == 0) || (strcasecmp(buffering_env, "dynamic") == 0))
        {
            core->buffering_mode = Core::BUFFERING_AUTO;
        }
    }

    enum wlr_log_importance log_level = WLR_INFO;
    if (getenv("SPARROW_DEBUG") || core->debug_protocol)
    {
        log_level = WLR_DEBUG;
    }

    wlr_log_init(log_level, nullptr);
    core->main_thread_id = pthread_self();

    wlr_log(WLR_INFO, "Starting sparrow: %s", get_version_string().c_str());
    const char *buf_mode_name = (core->buffering_mode ==
        Core::BUFFERING_DOUBLE) ? "DOUBLE BUFFERING (DB)" :
        (core->buffering_mode == Core::BUFFERING_AUTO) ? "DYNAMIC TRIPLE BUFFERING (AUTO)" :
        "FORCED TRIPLE BUFFERING (TB:ON)";
    wlr_log(WLR_INFO, "Buffering mode: %s", buf_mode_name);
    if (core->debug_damage)
    {
        wlr_log(WLR_INFO, "Damage visualization debug mode: ENABLED");
    }

    if (core->show_fps)
    {
        wlr_log(WLR_INFO, "FPS OSD monitor mode: ENABLED");
    }

    if (core->debug_protocol)
    {
        wlr_log(WLR_INFO, "Wayland protocol trace logging: ENABLED");
    }

#if defined (SPARROW_ENABLE_TRACE)
    const char *perfetto_path = nullptr;
    int rdoc_frame = -1;
    for (int i = 1; i < opts.argc; ++i)
    {
        if ((strcmp(opts.argv[i], "--trace-perfetto") == 0) ||
            (strcmp(opts.argv[i], "--perfetto") == 0))
        {
            perfetto_path = "out/sparrow.pftrace";
        } else if (strncmp(opts.argv[i], "--trace-perfetto=", 17) == 0)
        {
            perfetto_path = opts.argv[i] + 17;
        } else if (strncmp(opts.argv[i], "--perfetto=", 11) == 0)
        {
            perfetto_path = opts.argv[i] + 11;
        } else if (strcmp(opts.argv[i], "--renderdoc-capture") == 0)
        {
            rdoc_frame = 30;
        } else if (strncmp(opts.argv[i], "--renderdoc-capture=", 20) == 0)
        {
            rdoc_frame = atoi(opts.argv[i] + 20);
        }
    }

    SparrowTrace::instance().init(perfetto_path, rdoc_frame);
#endif

    core->embedder_api.struct_size = sizeof(FlutterEngineProcTable);
    if (FlutterEngineGetProcAddresses(&core->embedder_api) != kSuccess)
    {
        wlr_log(WLR_ERROR, "Could not get engine proc table");
        delete (core);
        return EXIT_FAILURE;
    }

    core->wl_display = wl_display_create();
    wlr_fixes_create(core->wl_display, 1);
    core->wl_event_loop = wl_display_get_event_loop(core->wl_display);

    wl_display_set_default_max_buffer_size(core->wl_display, 1024UL * 1024UL);

    core->backend =
        wlr_backend_autocreate(core->wl_event_loop, &core->session);
    if (core->backend == nullptr)
    {
        wlr_log(WLR_ERROR, "Failed to create wlr_backend");
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    wl_event_loop_fd_func_t callable_queue_function = [] (int fd, uint32_t mask,
                                                          void *data)
    {
        Core *instance = static_cast<Core*>(data);
        return (int)instance->callable_queue.execute();
    };

    core->callable_queue_event_source = wl_event_loop_add_fd(core->wl_event_loop,
        core->callable_queue.get_fd(), WL_EVENT_READABLE,
        callable_queue_function, core);

    core->fps_decay_timer = wl_event_loop_add_timer(
        core->wl_event_loop,
        [] (void *data) -> int
    {
        Core *inst = static_cast<Core*>(data);
        if (inst && inst->show_fps)
        {
            sparrow_damage_add_box(nullptr);
        }

        return 0;
    },
        core);

    int drm_fd = -1;
    char *drm_device = getenv("WLR_RENDER_DRM_DEVICE");
    if (drm_device)
    {
        drm_fd = open(drm_device, O_RDWR | O_CLOEXEC);
    } else
    {
        drm_fd = wlr_backend_get_drm_fd(core->backend);
    }

    if (drm_fd < 0)
    {
#if WLR_HAS_UDMABUF_ALLOCATOR == 1
        wlr_log(WLR_ERROR, "Failed to open DRM render device, consider specifying "
                           "WLR_RENDER_DRM_DEVICE."
                           "Trying SW rendering instead.");
#else
        wlr_log(WLR_ERROR, "Failed to open DRM render device, consider specifying "
                           "WLR_RENDER_DRM_DEVICE."
                           "If you want to use software rendering, ensure that "
                           "wlroots has been compiled with udmabuf "
                           "allocator support (available in wlroots >= 0.19.0) and "
                           "recompile Wayfire.");

        wl_display_destroy_clients(instance->wl_display);
        wl_display_destroy(instance->wl_display);
        pthread_mutex_destroy(&instance->platform_task_list_mutex);
        pthread_mutexattr_destroy(&mutex_attr);
        return EXIT_FAILURE;
#endif
    }

    wlr_egl *egl = wlr_egl_create_with_drm_fd(drm_fd);

    if (egl == nullptr)
    {
        wlr_log(WLR_ERROR, "Failed to create EGL");
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    core->egl = egl;

    wlr_renderer *renderer = wlr_gles2_renderer_create(egl);
    if (renderer == nullptr)
    {
        wlr_log(WLR_ERROR, "Failed to create GLES2 renderer");
        wlr_egl_destroy(egl);
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    core->renderer = renderer;

    // wlr_egl is opaque in 0.18 - extract EGL display/context via accessors
    core->egl_display = wlr_egl_get_display(egl);
    core->egl_context = wlr_egl_get_context(egl);

    if ((wlr_renderer_get_drm_fd(core->renderer) >= 0) &&
        core->renderer->features.timeline)
    {
        wlr_linux_drm_syncobj_manager_v1_create(
            core->wl_display, 1, wlr_renderer_get_drm_fd(core->renderer));
    }

    core->allocator =
        wlr_allocator_autocreate(core->backend, core->renderer);
    if (core->allocator == nullptr)
    {
        wlr_log(WLR_ERROR,
            "Failed to create allocator: neither DRM render node (/dev/dri) nor /dev/udmabuf is available");
        wlr_egl_destroy(egl);
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    if (!allow_root && !drop_permissions())
    {
        wlr_egl_destroy(egl);
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    wlr_renderer_init_wl_display(core->renderer, core->wl_display);

    wlr_compositor_create(core->wl_display, 6, core->renderer);
    wlr_subcompositor_create(
        core->wl_display); // Required by Firefox and other browsers

    wlr_data_device_manager_create(core->wl_display);

    // Primary selection (middle-click paste) - essential for Linux workflow
    wlr_primary_selection_v1_device_manager_create(core->wl_display);

    // Data control - clipboard access for wl-copy/wl-paste and some terminal
    // emulators (legacy wlr protocol and modern standard ext protocol)
    wlr_data_control_manager_v1_create(core->wl_display);
    wlr_ext_data_control_manager_v1_create(core->wl_display, 1);

    core->output_layout = wlr_output_layout_create(core->wl_display);
    core->scene = wlr_scene_create();
    core->scene_output_layout =
        wlr_scene_attach_output_layout(core->scene, core->output_layout);
    core->flutter_scene_buffer = nullptr;

    // Initialize multi-output support
    wl_list_init(&core->outputs);
    core->vsync_output     = nullptr;
    core->next_output_id   = 0;
    core->vsync_rate_limit = 0;

    wlr_color_representation_manager_v1_create_with_renderer(
        core->wl_display, 1, core->renderer);

    wlr_log(WLR_INFO, "instance->renderer->features.input_color_transform %i",
        core->renderer->features.input_color_transform);
    if (core->renderer->features.input_color_transform)
    {
        static const enum wp_color_manager_v1_render_intent render_intents[] = {
            WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL,
        };

        size_t transfer_functions_len = 0;
        enum wp_color_manager_v1_transfer_function *transfer_functions =
            wlr_color_manager_v1_transfer_function_list_from_renderer(
                core->renderer, &transfer_functions_len);

        size_t primaries_len = 0;
        enum wp_color_manager_v1_primaries *primaries =
            wlr_color_manager_v1_primaries_list_from_renderer(core->renderer,
                                                              &primaries_len);

        wlr_color_manager_v1_options cm_options{};
        cm_options.features.parametric = true;
        cm_options.features.set_mastering_display_primaries = true;
        cm_options.render_intents     = render_intents;
        cm_options.render_intents_len =
            sizeof(render_intents) / sizeof(render_intents[0]);
        cm_options.transfer_functions     = transfer_functions;
        cm_options.transfer_functions_len = transfer_functions_len;
        cm_options.primaries     = primaries;
        cm_options.primaries_len = primaries_len;

        auto color_manager_v1 =
            wlr_color_manager_v1_create(core->wl_display, 2, &cm_options);
        if (!color_manager_v1)
        {
            wlr_log(WLR_ERROR, "Failed to create wlr_color_manager_v1 global");
        }

        free(transfer_functions);
        free(primaries);
    } else
    {
        wlr_log(WLR_INFO, "Renderer does not support input color transforms; "
                          "wp_color_management_v1 will not be available.");
    }

    core->new_output.notify = sparrow_server_new_output;
    wl_signal_add(&core->backend->events.new_output, &core->new_output);

    // XDG output manager - provides output info to clients (needed by grim, etc.)
    wlr_xdg_output_manager_v1_create(core->wl_display,
        core->output_layout);

    // Output manager protocol - dynamic output configuration (wlr-randr,
    // wdisplays, kanshi)
    sparrow_output_manager_init();
    sparrow_output_power_manager_init();

    core->xdg_shell = wlr_xdg_shell_create(core->wl_display, 5);
    core->new_xdg_toplevel.notify = sparrow_new_xdg_toplevel;
    wl_signal_add(&core->xdg_shell->events.new_toplevel,
        &core->new_xdg_toplevel);

    wlr_tablet_v2_create(core->wl_display);

    // Screencopy - enables screenshots (grim) and screen recording
    wlr_screencopy_manager_v1_create(core->wl_display);

    // Export DMA-BUF - enables screen sharing (OBS, Discord, Zoom)
    wlr_export_dmabuf_manager_v1_create(core->wl_display);

    wlr_alpha_modifier_v1_create(core->wl_display);

    // Idle notification - lets apps know when user is idle (for screen lock,
    // power management)
    core->idle_notifier = wlr_idle_notifier_v1_create(core->wl_display);

    // Idle inhibit - allows apps to prevent idle (video playback, presentations)
    wlr_idle_inhibit_v1_create(core->wl_display);

    // Legacy KDE server decoration protocol - set default mode to SERVER
    // This tells older clients (GTK3, some Qt, Firefox) that we prefer
    // server-side decorations
    core->legacy_decoration_manager =
        wlr_server_decoration_manager_create(core->wl_display);
    wlr_server_decoration_manager_set_default_mode(
        core->legacy_decoration_manager,
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    core->new_server_decoration.notify = sparrow_handle_new_server_decoration;
    wl_signal_add(&core->legacy_decoration_manager->events.new_decoration,
        &core->new_server_decoration);
    wlr_log(WLR_INFO,
        "Enabled KDE server decoration protocol with SERVER mode default");

    // xdg-decoration: tell apps to use server-side decorations (we provide title
    // bars)
    core->decoration_manager =
        wlr_xdg_decoration_manager_v1_create(core->wl_display);
    core->new_toplevel_decoration.notify =
        sparrow_handle_new_toplevel_decoration;
    wl_signal_add(&core->decoration_manager->events.new_toplevel_decoration,
        &core->new_toplevel_decoration);

    // Foreign toplevel protocols for window management and listing
    core->foreign_toplevel_manager =
        wlr_foreign_toplevel_manager_v1_create(core->wl_display);
    if (core->foreign_toplevel_manager != nullptr)
    {
        wlr_log(WLR_INFO, "Enabled foreign toplevel management protocol "
                          "(wlr_foreign_toplevel_management_v1)");
    }

    core->ext_foreign_toplevel_list =
        wlr_ext_foreign_toplevel_list_v1_create(core->wl_display, 1);
    if (core->ext_foreign_toplevel_list != nullptr)
    {
        wlr_log(WLR_INFO, "Enabled ext-foreign-toplevel-list protocol "
                          "(ext_foreign_toplevel_list_v1)");
    }

    core->pointer_gestures =
        wlr_pointer_gestures_v1_create(core->wl_display);
    core->relative_pointer_manager =
        wlr_relative_pointer_manager_v1_create(core->wl_display);
    core->pointer_constraints =
        wlr_pointer_constraints_v1_create(core->wl_display);
    sparrow_pointer_constraints_init(core);

    core->virtual_keyboard_manager =
        wlr_virtual_keyboard_manager_v1_create(core->wl_display);
    if (core->virtual_keyboard_manager != nullptr)
    {
        core->new_virtual_keyboard.notify = handle_new_virtual_keyboard;
        wl_signal_add(
            &core->virtual_keyboard_manager->events.new_virtual_keyboard,
            &core->new_virtual_keyboard);
        wlr_log(WLR_INFO,
            "Enabled virtual keyboard protocol (wlr_virtual_keyboard_v1)");
    }

    core->virtual_pointer_manager =
        wlr_virtual_pointer_manager_v1_create(core->wl_display);
    if (core->virtual_pointer_manager != nullptr)
    {
        core->new_virtual_pointer.notify = handle_new_virtual_pointer;
        wl_signal_add(
            &core->virtual_pointer_manager->events.new_virtual_pointer,
            &core->new_virtual_pointer);
        wlr_log(WLR_INFO,
            "Enabled virtual pointer protocol (wlr_virtual_pointer_v1)");
    }

    wlr_input_method_manager_v2_create(core->wl_display);
    wlr_text_input_manager_v3_create(core->wl_display);

    core->presentation =
        wlr_presentation_create(core->wl_display, core->backend, 1);
    wlr_viewporter_create(core->wl_display);

    wlr_xdg_foreign_registry *foreign_registry =
        wlr_xdg_foreign_registry_create(core->wl_display);
    wlr_xdg_foreign_v1_create(core->wl_display, foreign_registry);
    wlr_xdg_foreign_v2_create(core->wl_display, foreign_registry);

    wlr_fractional_scale_manager_v1_create(core->wl_display, 1);
    wlr_single_pixel_buffer_manager_v1_create(core->wl_display);
    wlr_content_type_manager_v1_create(core->wl_display, 1);

    // xdg-activation-v1: allows applications to transfer focus / activate
    // surfaces
    core->xdg_activation = wlr_xdg_activation_v1_create(core->wl_display);
    if (core->xdg_activation != nullptr)
    {
        core->xdg_activation_request_activate.notify =
            sparrow_handle_xdg_activation_request_activate;
        wl_signal_add(&core->xdg_activation->events.request_activate,
            &core->xdg_activation_request_activate);
        wlr_log(WLR_INFO, "Enabled xdg-activation-v1 protocol");
    }

    core->ipc_server = new IpcServer(core);
    if (!core->ipc_server->init())
    {
        wlr_log(WLR_ERROR, "Failed to initialize Sparrow IPC server");
    }

    increase_nofile_limit();

    // Handle popup surfaces (menus, dropdowns, tooltips)
    core->new_xdg_popup.notify = sparrow_new_xdg_popup;
    wl_signal_add(&core->xdg_shell->events.new_popup,
        &core->new_xdg_popup);

    auto socket = choose_socket(core->wl_display);
    if (!socket)
    {
        wlr_log(WLR_ERROR, "Failed to create Wayland socket");
        wlr_egl_destroy(egl);
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    sparrow_seat_init();

    core->wl_socket = socket.value().c_str();
    if (!wlr_backend_start(core->backend))
    {
        wlr_log(WLR_ERROR, "Failed to initialize backend, exiting");
        wlr_egl_destroy(egl);
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    setenv("WAYLAND_DISPLAY", core->wl_socket, 1);
    setenv("XDG_SESSION_TYPE", "wayland", true);
    setenv("DISPLAY", ":0", true);

    // instance->views = handle_map_new();
    core->subsurfaces = handle_map_new();
    core->popups = handle_map_new();
    wl_list_init(&core->views_list);

    sparrow_renderer_init(eglGetProcAddress);

    sparrow_tasks_init();

    sparrow_udmabuf_init();

    sparrow_enable_realtime_scheduling();

    sparrow_dispatcher_init(core->wl_display);

    struct wl_event_loop *event_loop =
        wl_display_get_event_loop(core->wl_display);
    core->sigint_event_source = wl_event_loop_add_signal(event_loop, SIGINT, handle_term_signal,
        core);
    core->sigterm_event_source = wl_event_loop_add_signal(event_loop, SIGTERM, handle_term_signal,
        core);

    FlutterRendererConfig renderer_config = {};
    renderer_config.type = kOpenGL;
    renderer_config.open_gl.struct_size   = sizeof(FlutterOpenGLRendererConfig);
    renderer_config.open_gl.make_current  = engine_cb_renderer_make_current;
    renderer_config.open_gl.clear_current = engine_cb_renderer_clear_current;
    renderer_config.open_gl.make_resource_current =
        engine_cb_renderer_make_resource_current;
    renderer_config.open_gl.fbo_reset_after_present = true;
    renderer_config.open_gl.gl_proc_resolver = engine_cb_renderer_gl_proc_resolve;
    renderer_config.open_gl.gl_external_texture_frame_callback =
        engine_cb_external_texture;
    renderer_config.open_gl.fbo_with_frame_info_callback = engine_cb_renderer_fbo;
    renderer_config.open_gl.present_with_info = engine_cb_renderer_present;

    FlutterProjectArgs project_args = {};
    project_args.struct_size = sizeof(FlutterProjectArgs);
    project_args.command_line_argc = opts.argc;
    project_args.command_line_argv = opts.argv;
    project_args.assets_path   = opts.assets_path.c_str();
    project_args.icu_data_path = opts.icu_data_path.c_str();
    project_args.platform_message_callback = engine_cb_platform_message;
    project_args.log_message_callback = engine_cb_log_message;
    project_args.custom_task_runners  = &core->custom_task_runners;
    if (opts.argc > 1)
    {
        project_args.dart_entrypoint_argc = opts.argc - 1;
        project_args.dart_entrypoint_argv = opts.argv + 1;
    }

#ifdef FLUTTER_COMPOSITOR
    project_args.compositor = &core->fl_compositor;
#endif
    project_args.vsync_callback = sparrow_engine_vsync_callback;
    project_args.shutdown_dart_vm_when_done = true;
    project_args.engine_id = reinterpret_cast<int64_t>(core);

    if (core->embedder_api.RunsAOTCompiledDartCode())
    {
        FlutterEngineAOTDataSource aot_source = {};
        FlutterEngineAOTData aot_data;

        aot_source    = {
            .type     = kFlutterEngineAOTDataSourceTypeElfPath,
            .elf_path = opts.elf_file_path.c_str(),
        };

        const FlutterEngineResult engine_result =
            FlutterEngineCreateAOTData(&aot_source, &aot_data);
        if (engine_result != kSuccess)
        {
            wlr_log(WLR_ERROR,
                "Could not load AOT data. FlutterEngineCreateAOTData failed.");
            wlr_egl_destroy(egl);
            wl_display_destroy_clients(core->wl_display);
            wl_display_destroy(core->wl_display);
            delete (core);
            return EXIT_FAILURE;
        }

        project_args.aot_data = aot_data;
    }

    // Initialize the flutter:: client wrapper messaging infrastructure
    core->message_dispatcher =
        std::make_unique<IncomingMessageDispatcher>(&core->messenger);
    core->messenger.SetMessageDispatcher(core->message_dispatcher.get());

    sparrow_text_input_init();
    sparrow_cursor_init();
    sparrow_keyboard_channel_init();
    sparrow_isolate_channel_init();
    sparrow_engine_init_channels();

    const FlutterEngineResult fl_result = core->embedder_api.Run(
        FLUTTER_ENGINE_VERSION, &renderer_config, &project_args, (void*)core,
        &core->engine);

    if (fl_result != kSuccess)
    {
        wlr_log(WLR_ERROR, "Flutter Engine Run failed!");
    }

    // Connect the BinaryMessenger to the running engine (must be after Run)
    core->messenger.SetEngine(core->engine, &core->embedder_api);

    // Note: Outputs are sent to Flutter when Dart signals "compositor_ready"
    // This ensures Dart's message handlers are registered before we send data

    // Send initial window metrics based on total output bounds
    if (!wl_list_empty(&core->outputs))
    {
        struct wlr_box total_box = {};
        wlr_output_layout_get_box(core->output_layout, nullptr, &total_box);

        FlutterWindowMetricsEvent window_metrics = {};
        window_metrics.struct_size = sizeof(FlutterWindowMetricsEvent);
        window_metrics.width  = total_box.width;
        window_metrics.height = total_box.height;
        // Keep Flutter's coordinate space consistent - use 1.0 for multi-output
        // Individual outputs may have different scales, handled per-output
        window_metrics.pixel_ratio = 1.0;
        wlr_log(WLR_INFO, "Sending Flutter window metrics: %dx%d, pixel_ratio=%.2f",
            total_box.width, total_box.height, window_metrics.pixel_ratio);
        core->embedder_api.SendWindowMetricsEvent(core->engine,
            &window_metrics);
    }

    FlutterPointerEvent pointer_event = {};
    pointer_event.struct_size = sizeof(FlutterPointerEvent);
    pointer_event.phase     = kAdd;
    pointer_event.timestamp = core->embedder_api.GetCurrentTime() / 1000;
    pointer_event.x = 0;
    pointer_event.y = 0;
    pointer_event.device = 0;
    pointer_event.signal_kind    = kFlutterPointerSignalKindNone;
    pointer_event.scroll_delta_x = 0;
    pointer_event.scroll_delta_y = 0;
    pointer_event.device_kind    = kFlutterPointerDeviceKindMouse;
    pointer_event.buttons = 0;
    core->embedder_api.SendPointerEvent(core->engine, &pointer_event, 1);

    wlr_log(WLR_INFO, "Engine Run success!");

    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
        core->wl_socket);

    wl_display_run(core->wl_display);
    if (exit_because_signal == SIGINT)
    {
        wlr_log(WLR_INFO, "Got SIGINT, shutting down");
    } else if (exit_because_signal == SIGTERM)
    {
        wlr_log(WLR_INFO, "Got SIGTERM, shutting down");
    }

    sparrow_engine_reset_channels();

    wl_display_destroy_clients(core->wl_display);
    engine_dispose(core->engine, project_args.aot_data);

    if (core->new_xdg_toplevel.link.prev != nullptr)
    {
        wl_list_remove(&core->new_xdg_toplevel.link);
    }

    if (core->new_xdg_popup.link.prev != nullptr)
    {
        wl_list_remove(&core->new_xdg_popup.link);
    }

    if (core->new_toplevel_decoration.link.prev != nullptr)
    {
        wl_list_remove(&core->new_toplevel_decoration.link);
    }

    if (core->new_server_decoration.link.prev != nullptr)
    {
        wl_list_remove(&core->new_server_decoration.link);
    }

    if (core->new_virtual_keyboard.link.prev != nullptr)
    {
        wl_list_remove(&core->new_virtual_keyboard.link);
    }

    if (core->new_virtual_pointer.link.prev != nullptr)
    {
        wl_list_remove(&core->new_virtual_pointer.link);
    }

    if (core->new_output.link.prev != nullptr)
    {
        wl_list_remove(&core->new_output.link);
        wl_list_init(&core->new_output.link);
    }

    if ((core->output_manager_apply.link.prev != nullptr) &&
        (core->output_manager_apply.link.next != &core->output_manager_apply.link))
    {
        wl_list_remove(&core->output_manager_apply.link);
        wl_list_init(&core->output_manager_apply.link);
    }

    if ((core->output_manager_test.link.prev != nullptr) &&
        (core->output_manager_test.link.next != &core->output_manager_test.link))
    {
        wl_list_remove(&core->output_manager_test.link);
        wl_list_init(&core->output_manager_test.link);
    }

    if ((core->output_power_manager_set_mode.link.prev != nullptr) &&
        (core->output_power_manager_set_mode.link.next != &core->output_power_manager_set_mode.link))
    {
        wl_list_remove(&core->output_power_manager_set_mode.link);
        wl_list_init(&core->output_power_manager_set_mode.link);
    }

    if ((core->xdg_activation_request_activate.link.prev != nullptr) &&
        (core->xdg_activation_request_activate.link.next !=
         &core->xdg_activation_request_activate.link))
    {
        wl_list_remove(&core->xdg_activation_request_activate.link);
        wl_list_init(&core->xdg_activation_request_activate.link);
    }

    if (core->engine != nullptr)
    {
        core->embedder_api.Shutdown(core->engine);
        core->engine = nullptr;
        core->messenger.Shutdown();
    }

    sparrow_seat_finish();
    sparrow_renderer_destroy();
    sparrow_udmabuf_finish();
    sparrow_dispatcher_finish();

    Output *out, *out_tmp;
    wl_list_for_each_safe(out, out_tmp, &core->outputs, link)
    {
        if ((out->destroy.link.prev != nullptr) && (out->destroy.link.next != nullptr))
        {
            wl_list_remove(&out->destroy.link);
            wl_list_init(&out->destroy.link);
        }

        if ((out->frame.link.prev != nullptr) && (out->frame.link.next != nullptr))
        {
            wl_list_remove(&out->frame.link);
            wl_list_init(&out->frame.link);
        }

        if ((out->request_state.link.prev != nullptr) && (out->request_state.link.next != nullptr))
        {
            wl_list_remove(&out->request_state.link);
            wl_list_init(&out->request_state.link);
        }

        if ((out->present.link.prev != nullptr) && (out->present.link.next != nullptr))
        {
            wl_list_remove(&out->present.link);
            wl_list_init(&out->present.link);
        }

        if (out->scene_output != nullptr)
        {
            wlr_scene_output_destroy(out->scene_output);
            out->scene_output = nullptr;
        }

        if ((core->output_layout != nullptr) && (out->wlr_output != nullptr))
        {
            wlr_output_layout_remove(core->output_layout, out->wlr_output);
        }

        wl_list_remove(&out->link);
        wl_list_init(&out->link);

        if (out->osd_texture != nullptr)
        {
            wlr_texture_destroy(out->osd_texture);
            out->osd_texture = nullptr;
        }

        wlr_damage_ring_finish(&out->damage_ring);
        pixman_region32_fini(&out->client_damage);
        pthread_mutex_destroy(&out->damage_mutex);
#ifdef DAMAGE_HISTORY
        for (int i = 0; i < NUM_DAMAGE_HISTORY; i++)
        {
            pixman_region32_fini(&out->damage_history[i]);
        }

#endif
        delete out;
    }

    if (core->scene != nullptr)
    {
        wlr_scene_node_destroy(&core->scene->tree.node);
        core->scene = nullptr;
    }

    if (core->output_layout != nullptr)
    {
        wlr_output_layout_destroy(core->output_layout);
        core->output_layout = nullptr;
    }

    if (core->allocator != nullptr)
    {
        wlr_allocator_destroy(core->allocator);
        core->allocator = nullptr;
    }

    if (core->renderer != nullptr)
    {
        wlr_renderer_destroy(core->renderer);
        core->renderer = nullptr;
    }

    sparrow_tasks_finish();

    if (core->callable_queue_event_source != nullptr)
    {
        wl_event_source_remove(core->callable_queue_event_source);
        core->callable_queue_event_source = nullptr;
    }

    if (core->sigint_event_source != nullptr)
    {
        wl_event_source_remove(core->sigint_event_source);
        core->sigint_event_source = nullptr;
    }

    if (core->sigterm_event_source != nullptr)
    {
        wl_event_source_remove(core->sigterm_event_source);
        core->sigterm_event_source = nullptr;
    }

    if (core->fps_decay_timer != nullptr)
    {
        wl_event_source_remove(core->fps_decay_timer);
        core->fps_decay_timer = nullptr;
    }

    struct wl_display *wl_display = core->wl_display;
    delete (core);
    wl_display_destroy(wl_display);
    wlr_log(WLR_INFO, "Shutdown successful!");
    return EXIT_SUCCESS;
}
