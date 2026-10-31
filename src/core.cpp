#include <algorithm>
#include <assert.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <optional>
#include <signal.h>
#include <sys/resource.h>

#include "core.hpp"

#include <sparrow/nonstd/wlroots-full.hpp>
#include <sparrow/options.hpp>

#include "export_dmabuf/sparrow_export_dmabuf.hpp"
#include "flutter/platform/cursor.hpp"
#include "flutter/platform/engine.hpp"
#include "flutter/platform/engine/messages/output_message.hpp"
#include "flutter/platform/isolate.hpp"
#include "flutter/platform/keyboard_channel.hpp"
#include "flutter/platform/restoration.hpp"
#include "flutter/platform/task.hpp"
#include "flutter/platform/text_input.hpp"
#include "input/keyboard.hpp"
#include "input/pointer.hpp"
#include "input/seat.hpp"
#include "ipc/ipc_server.hpp"
#include "output.hpp"
#include "screencopy/sparrow_screencopy.hpp"
#include "surface/decoration.hpp"
#include "surface/popup.hpp"
#include "surface/secure_output.hpp"
#include "surface/session_lock.hpp"
#include "surface/session_manifest.hpp"
#include "surface/sub_surface.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"
#include "util/dispatcher.hpp"
#include "util/handle_map.hpp"
#include "util/process_supervisor.hpp"
#include "util/realtime.hpp"
#include "util/udmabuf.hpp"
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
    } priv;
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
    struct wlr_fixes *fixes =
        wl_container_of(listener, fixes, priv.display_destroy);
    wl_signal_emit_mutable(&fixes->events.destroy, nullptr);

    assert(wl_list_empty(&fixes->events.destroy.listener_list));

    wl_list_remove(&fixes->priv.display_destroy.link);
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

    fixes->priv.display_destroy.notify = fixes_handle_display_destroy;
    wl_display_add_destroy_listener(display, &fixes->priv.display_destroy);

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
    const auto instance = const_cast<Core*>(static_cast<Core*>(data));
    exit_because_signal = signal;
    sparrow_restoration_flush_to_disk();
    sparrow_session_manifest_flush();
    if (instance && instance->wl_display)
    {
        wl_display_terminate(instance->wl_display);
    }

    return 0;
}

void Core::restart_flutter_engine()
{
    wlr_log(WLR_INFO,
        "[GPU-RESET] Starting Flutter Engine in-process restart...");

    // 1. Mark existing views, subsurfaces, and popups as needing texture
    // re-registration
    SparrowView *v = nullptr;
    wl_list_for_each(v, &views_list, link)
    {
        v->texture_registered = false;
        SparrowSubSurface *sub = nullptr;
        wl_list_for_each(sub, &v->subsurfaces, link)
        {
            sub->texture_registered = false;
        }
    }
    if (popups)
    {
        for (const auto &[handle, value] : *popups)
        {
            auto *popup = static_cast<SparrowPopup*>(value);
            if (popup)
            {
                popup->texture_registered = false;
            }
        }
    }

    // 2. Re-initialize message dispatcher & channel handlers
    message_dispatcher = std::make_unique<IncomingMessageDispatcher>(&messenger);
    messenger.SetMessageDispatcher(message_dispatcher.get());

    sparrow_text_input_init();
    sparrow_cursor_init();
    sparrow_keyboard_channel_init();
    sparrow_isolate_channel_init();
    sparrow_engine_init_channels();

    // 3. Initialize fresh FlutterEngine instance before running so custom task
    // runners have a valid handle
    FlutterEngine new_engine = nullptr;
    const FlutterEngineResult init_result =
        embedder_api.Initialize(FLUTTER_ENGINE_VERSION, &flutter_renderer_config,
            &flutter_project_args, (void*)this, &new_engine);

    if ((init_result != kSuccess) || (new_engine == nullptr))
    {
        wlr_log(WLR_ERROR,
            "[GPU-RESET] Failed to initialize Flutter Engine (result=%d)!",
            (int)init_result);
        return;
    }

    engine = new_engine;
    messenger.SetEngine(engine, &embedder_api);

    const FlutterEngineResult run_result =
        embedder_api.RunInitialized(new_engine);
    if (run_result != kSuccess)
    {
        wlr_log(WLR_ERROR,
            "[GPU-RESET] Failed to run initialized Flutter Engine (result=%d)!",
            (int)run_result);
        return;
    }

    // 4. Send initial window metrics
    if (!wl_list_empty(&outputs))
    {
        struct wlr_box total_box = {};
        if (output_layout != nullptr)
        {
            wlr_output_layout_get_box(output_layout, nullptr, &total_box);
        }

        if ((total_box.width > 0) && (total_box.height > 0))
        {
            FlutterWindowMetricsEvent window_metrics = {};
            window_metrics.struct_size = sizeof(FlutterWindowMetricsEvent);
            window_metrics.width  = total_box.width;
            window_metrics.height = total_box.height;
            window_metrics.pixel_ratio = 1.0;
            embedder_api.SendWindowMetricsEvent(engine, &window_metrics);
        }
    }

    FlutterPointerEvent pointer_event = {};
    pointer_event.struct_size = sizeof(FlutterPointerEvent);
    pointer_event.phase     = kAdd;
    pointer_event.timestamp = embedder_api.GetCurrentTime() / 1000;
    pointer_event.x = cursor ? cursor->x : 0.0;
    pointer_event.y = cursor ? cursor->y : 0.0;
    pointer_event.device_kind = kFlutterPointerDeviceKindMouse;
    embedder_api.SendPointerEvent(engine, &pointer_event, 1);

    wlr_log(WLR_INFO, "[GPU-RESET] Flutter Engine cleanly restarted in-process!");
}

static void do_renderer_recreate(void *data)
{
    const auto core = static_cast<Core*>(data);
    core->recreating_renderer = nullptr;
    core->is_gpu_resetting    = true;

    wlr_log(WLR_INFO,
        "[GPU-RESET] Re-creating renderer & allocator after GPU reset...");

    // 1. Shutdown old engine and tasks FIRST so render/IO threads stop using old
    // EGL contexts. Crucially, do NOT hold render_mutex/texture_mutex during
    // Shutdown()! The rasterizer thread may be executing present_layers or
    // external_texture, and if it blocks on those mutexes while Shutdown()
    // waits for the thread to exit, a deadlock / soft-lock occurs.
    if (core->engine != nullptr)
    {
        // Return any pending vsync baton to the old engine before shutdown
        intptr_t old_baton = atomic_exchange(&core->vsync_baton, 0);
        if (old_baton != 0)
        {
            uint64_t current_time = core->embedder_api.GetCurrentTime();
            core->embedder_api.OnVsync(core->engine, old_baton, current_time,
                current_time + 16600000);
        }

        {
            std::scoped_lock lock(core->platform_task_mutex);
            core->queued_platform_tasks.clear();
        }
        sparrow_engine_reset_channels();
        core->messenger.Shutdown();
        core->messenger.SetEngine(nullptr, nullptr);
        core->embedder_api.Shutdown(core->engine);
        core->engine = nullptr;
    }

    // Now all secondary engine threads (rasterizer, IO) are guaranteed joined and
    // dead. Acquire renderer locks safely for the remainder of the recreation
    // pipeline.
    std::lock_guard<std::recursive_mutex> render_lock(
        core->sparrow_renderer.render_mutex);
    std::lock_guard<std::recursive_mutex> texture_lock(
        core->sparrow_renderer.texture_mutex);

    // Engine threads are gone: free the old Flutter EGL contexts while the
    // old EGL display is still the one they were created on.
    sparrow_renderer_release_egl_contexts();

    int drm_fd = core->drm_fd;
    if ((drm_fd < 0) && (core->backend != nullptr))
    {
        drm_fd = wlr_backend_get_drm_fd(core->backend);
    }

    if ((drm_fd < 0) && (core->renderer != nullptr))
    {
        drm_fd = wlr_renderer_get_drm_fd(core->renderer);
    }

    struct wlr_renderer *renderer = nullptr;
    if (drm_fd >= 0)
    {
        renderer = wlr_gles2_renderer_create_with_drm_fd(drm_fd);
    }

    if (!renderer && (core->backend != nullptr))
    {
        renderer = wlr_renderer_autocreate(core->backend);
    }

    if (!renderer)
    {
        wlr_log(WLR_ERROR, "[GPU-RESET] Unable to recreate wlr_gles2_renderer!");
        core->is_gpu_resetting = false;
        return;
    }

    struct wlr_allocator *allocator =
        wlr_allocator_autocreate(core->backend, renderer);
    if (!allocator)
    {
        wlr_log(WLR_ERROR, "[GPU-RESET] Unable to recreate wlr_allocator!");
        wlr_renderer_destroy(renderer);
        core->is_gpu_resetting = false;
        return;
    }

    struct wlr_renderer *old_renderer   = core->renderer;
    struct wlr_allocator *old_allocator = core->allocator;
    EGLDisplay old_egl_display = core->egl_display;
    core->renderer  = renderer;
    core->allocator = allocator;
    core->egl = wlr_gles2_renderer_get_egl(renderer);
    if (core->egl != nullptr)
    {
        core->egl_display = wlr_egl_get_display(core->egl);
        core->egl_context = wlr_egl_get_context(core->egl);
    }

    wl_list_remove(&core->renderer_lost.link);
    wl_signal_add(&core->renderer->events.lost, &core->renderer_lost);

    if (core->compositor)
    {
        wlr_compositor_set_renderer(core->compositor, renderer);
    }

    // Re-initialize SparrowRenderer EGL contexts and shaders on new display
    sparrow_renderer_recover_gpu_reset();

    if (core->xcursor_texture != nullptr)
    {
        wlr_texture_destroy(core->xcursor_texture);
        core->xcursor_texture = nullptr;
    }

    core->xcursor_texture_name = "";

    Output *output = nullptr;
    wl_list_for_each(output, &core->outputs, link)
    {
        if (output->osd_texture != nullptr)
        {
            wlr_texture_destroy(output->osd_texture);
            output->osd_texture = nullptr;
        }

        if (output->wlr_output)
        {
            wlr_output_init_render(output->wlr_output, core->allocator,
                core->renderer);
            if (output->wlr_output->enabled)
            {
                wlr_output_schedule_frame(output->wlr_output);
            }
        }
    }

    // Rehydrate textures for all existing surfaces on the main thread BEFORE
    // destroying old_renderer so that soft-reset can read back composited SHM
    // textures (which contain incremental glTexSubImage2D damage updates).
    static auto rehydrate_iter = [] (struct wlr_surface *surface, int sx, int sy,
                                     void *data)
    {
        (void)sx;
        (void)sy;
        (void)data;
        sparrow_surface_rehydrate_texture(surface);
    };

    struct timespec frame_now;
    clock_gettime(CLOCK_MONOTONIC, &frame_now);
    static auto frame_done_iter = [] (struct wlr_surface *surface, int sx, int sy,
                                      void *data)
    {
        (void)sx;
        (void)sy;
        wlr_surface_send_frame_done(surface, static_cast<struct timespec*>(data));
    };

    SparrowView *view_entry = nullptr;
    wl_list_for_each(view_entry, &core->views_list, link)
    {
        if (view_entry->xdg_surface && view_entry->xdg_surface->surface)
        {
            wlr_surface_for_each_surface(view_entry->xdg_surface->surface,
                rehydrate_iter, nullptr);
            wlr_surface_for_each_surface(view_entry->xdg_surface->surface,
                frame_done_iter, &frame_now);
            if (view_entry->xdg_surface->surface->mapped &&
                view_entry->xdg_surface->initialized)
            {
                wlr_xdg_surface_schedule_configure(view_entry->xdg_surface);
            }
        }

        SparrowSubSurface *sub = nullptr;
        wl_list_for_each(sub, &view_entry->subsurfaces, link)
        {
            if (sub->surface)
            {
                sparrow_surface_rehydrate_texture(sub->surface);
                wlr_surface_send_frame_done(sub->surface, &frame_now);
            }
        }
    }

    if (core->popups)
    {
        for (const auto &[handle, value] : *core->popups)
        {
            auto *popup = static_cast<SparrowPopup*>(value);
            if (popup && popup->xdg_surface && popup->xdg_surface->surface)
            {
                wlr_surface_for_each_surface(popup->xdg_surface->surface,
                    rehydrate_iter, nullptr);
                wlr_surface_for_each_surface(popup->xdg_surface->surface,
                    frame_done_iter, &frame_now);
            }
        }
    }

    SparrowSessionLockSurface *lock_surf = nullptr;
    wl_list_for_each(lock_surf, &core->session_lock_surfaces, link)
    {
        if (lock_surf->wlr_lock_surface && lock_surf->wlr_lock_surface->surface)
        {
            wlr_surface_for_each_surface(lock_surf->wlr_lock_surface->surface,
                rehydrate_iter, nullptr);
            wlr_surface_for_each_surface(lock_surf->wlr_lock_surface->surface,
                frame_done_iter, &frame_now);
        }
    }

    wlr_allocator_destroy(old_allocator);
    wlr_renderer_destroy(old_renderer);
    if ((old_egl_display != EGL_NO_DISPLAY) &&
        (old_egl_display != core->egl_display))
    {
        eglTerminate(old_egl_display);
    }

    core->is_gpu_resetting = false;
    core->restart_flutter_engine();
    wlr_log(WLR_INFO, "[GPU-RESET] Renderer recreated, engine restarted, and "
                      "surfaces fully rehydrated.");
}

static void handle_renderer_lost(struct wl_listener *listener, void *data)
{
    (void)listener;
    (void)data;
    const auto core = Core::instance();
    if (!core)
    {
        return;
    }

    if ((core->recreating_renderer != nullptr) || core->is_gpu_resetting.load())
    {
        wlr_log(
            WLR_DEBUG,
            "[GPU-RESET] Re-creation of renderer already scheduled or in progress");
        return;
    }

    wlr_log(WLR_INFO, "[GPU-RESET] Hardware GPU reset detected via "
                      "wlr_renderer->events.lost! Scheduling re-creation...");
    core->recreating_renderer =
        wl_event_loop_add_idle(core->wl_event_loop, do_renderer_recreate, core);
}

static int handle_gpu_reset_signal(int signal, void *data)
{
    (void)signal;
    const auto core = const_cast<Core*>(static_cast<Core*>(data));
    if (!core || (core->recreating_renderer != nullptr) ||
        core->is_gpu_resetting.load())
    {
        wlr_log(
            WLR_INFO,
            "[SIGNAL] SIGUSR2 ignored: GPU reset already scheduled or in progress");
        return 0;
    }

    if (!core->debug_gpureset)
    {
        wlr_log(WLR_INFO, "[SIGNAL] SIGUSR2 ignored: GPU reset simulation disabled "
                          "(enable with --debug-gpureset)");
        return 0;
    }

    wlr_log(WLR_INFO, "[SIGNAL] SIGUSR2 received: triggering simulated renderer "
                      "lost via wlr_renderer->events.lost");
    if (core->renderer)
    {
        wl_signal_emit_mutable(&core->renderer->events.lost, nullptr);
    } else if (core->wl_event_loop)
    {
        core->recreating_renderer =
            wl_event_loop_add_idle(core->wl_event_loop, do_renderer_recreate, core);
    } else
    {
        sparrow_engine_recover_gpu_reset();
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
    auto instance = _instance.load(std::memory_order_relaxed);
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

Core::Core()
{
    wl_list_init(&views_list);
    wl_list_init(&keyboards);
    wl_list_init(&touchs);
    wl_list_init(&pointers);
    wl_list_init(&idle_inhibitors);
    wl_list_init(&session_lock_surfaces);
    wl_list_init(&outputs);
    wl_list_init(&renderer_lost.link);

    memset(&new_xdg_toplevel, 0, sizeof(new_xdg_toplevel));
    memset(&new_xdg_popup, 0, sizeof(new_xdg_popup));
    memset(&new_toplevel_decoration, 0, sizeof(new_toplevel_decoration));
    memset(&new_server_decoration, 0, sizeof(new_server_decoration));
    memset(&client_cursor_destroy, 0, sizeof(client_cursor_destroy));
    memset(&cursor_motion, 0, sizeof(cursor_motion));
    memset(&cursor_motion_absolute, 0, sizeof(cursor_motion_absolute));
    memset(&cursor_button, 0, sizeof(cursor_button));
    memset(&cursor_axis, 0, sizeof(cursor_axis));
    memset(&cursor_frame, 0, sizeof(cursor_frame));
    memset(&swipe_begin, 0, sizeof(swipe_begin));
    memset(&swipe_update, 0, sizeof(swipe_update));
    memset(&swipe_end, 0, sizeof(swipe_end));
    memset(&pinch_begin, 0, sizeof(pinch_begin));
    memset(&pinch_update, 0, sizeof(pinch_update));
    memset(&pinch_end, 0, sizeof(pinch_end));
    memset(&hold_begin, 0, sizeof(hold_begin));
    memset(&hold_end, 0, sizeof(hold_end));
    memset(&cursor_touch_down, 0, sizeof(cursor_touch_down));
    memset(&cursor_touch_up, 0, sizeof(cursor_touch_up));
    memset(&cursor_touch_motion, 0, sizeof(cursor_touch_motion));
    memset(&cursor_touch_frame, 0, sizeof(cursor_touch_frame));
    memset(&cursor_touch_cancel, 0, sizeof(cursor_touch_cancel));
    memset(&request_cursor, 0, sizeof(request_cursor));
    memset(&new_virtual_keyboard, 0, sizeof(new_virtual_keyboard));
    memset(&new_virtual_pointer, 0, sizeof(new_virtual_pointer));
    memset(&new_input, 0, sizeof(new_input));
    memset(&request_set_selection, 0, sizeof(request_set_selection));
    memset(&request_set_primary_selection, 0,
        sizeof(request_set_primary_selection));
    memset(&request_set_cursor_shape, 0, sizeof(request_set_cursor_shape));
    memset(&start_drag, 0, sizeof(start_drag));
    memset(&new_idle_inhibitor, 0, sizeof(new_idle_inhibitor));
    memset(&new_session_lock, 0, sizeof(new_session_lock));
    memset(&session_lock_unlock, 0, sizeof(session_lock_unlock));
    memset(&session_lock_destroy, 0, sizeof(session_lock_destroy));
    memset(&session_lock_new_surface, 0, sizeof(session_lock_new_surface));
    memset(&new_pointer_constraint, 0, sizeof(new_pointer_constraint));
    memset(&xdg_activation_request_activate, 0,
        sizeof(xdg_activation_request_activate));
    memset(&new_output, 0, sizeof(new_output));
    memset(&output_manager_apply, 0, sizeof(output_manager_apply));
    memset(&output_manager_test, 0, sizeof(output_manager_test));
    memset(&output_power_manager_set_mode, 0,
        sizeof(output_power_manager_set_mode));
}

Core::~Core()
{
    wlr_log(WLR_INFO, "destroy core:%d", _instance != nullptr);
    sparrow_restoration_flush_to_disk();
    _instance.store(nullptr, std::memory_order_release);

    if (xdg_activation_request_activate.link.next &&
        xdg_activation_request_activate.link.prev &&
        (xdg_activation_request_activate.link.next !=
         &xdg_activation_request_activate.link))
    {
        wl_list_remove(&xdg_activation_request_activate.link);
        wl_list_init(&xdg_activation_request_activate.link);
    }

    sparrow_session_lock_finish(this);
    sparrow_idle_inhibit_finish(this);

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

    if (pointer_bottom_dwell_timer != nullptr)
    {
        wl_event_source_remove(pointer_bottom_dwell_timer);
        pointer_bottom_dwell_timer = nullptr;
    }

    if (scanout_settle_timer != nullptr)
    {
        wl_event_source_remove(scanout_settle_timer);
        scanout_settle_timer = nullptr;
    }

    if (process_supervisor)
    {
        process_supervisor->shutdown();
        delete process_supervisor;
        process_supervisor = nullptr;
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

bool Core::is_app_sensitive(const char *str, bool is_title) const
{
    if (!str || (str[0] == '\0'))
    {
        return false;
    }

    std::string haystack(str);
    std::transform(
        haystack.begin(), haystack.end(), haystack.begin(),
        [] (unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto & pattern : sensitive_app_ids)
    {
        if (pattern.empty())
        {
            continue;
        }

        std::string needle = pattern;
        std::transform(
            needle.begin(), needle.end(), needle.begin(),
            [] (unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (is_title)
        {
            if (haystack.find(needle) != std::string::npos)
            {
                return true;
            }
        } else
        {
            if ((haystack == needle) ||
                (haystack.find(needle) != std::string::npos))
            {
                return true;
            }
        }
    }

    return false;
}

void Core::recompute_view_sensitivity(SparrowView *view)
{
    if (!view)
    {
        return;
    }

    bool new_sensitive = view->ipc_sensitive;

    if (!new_sensitive && view->xdg_surface && view->xdg_surface->surface &&
        sparrow_surface_has_secure_output(view->xdg_surface->surface))
    {
        new_sensitive = true;
    }

    if (!new_sensitive && view->toplevel)
    {
        if ((view->toplevel->app_id &&
             is_app_sensitive(view->toplevel->app_id, false)) ||
            (view->toplevel->title &&
             is_app_sensitive(view->toplevel->title, true)))
        {
            new_sensitive = true;
        }
    }

    if (view->is_sensitive != new_sensitive)
    {
        view->is_sensitive = new_sensitive;
        wlr_log(WLR_INFO,
            "[SENSITIVE] View %u (app_id='%s', title='%s') sensitivity -> %s",
            view->handle,
            (view->toplevel && view->toplevel->app_id) ? view->toplevel->app_id :
            "",
            (view->toplevel && view->toplevel->title) ? view->toplevel->title :
            "",
            new_sensitive ? "true" : "false");
        sparrow_damage_add_box(nullptr);
    }
}

void Core::recompute_all_views_sensitivity()
{
    if (wl_list_empty(&views_list))
    {
        return;
    }

    SparrowView *view = nullptr;
    wl_list_for_each(view, &views_list, link)
    {
        recompute_view_sensitivity(view);
    }
}

void Core::set_sensitive_app_ids(std::vector<std::string> app_ids)
{
    sensitive_app_ids = std::move(app_ids);
    wlr_log(
        WLR_INFO,
        "[SENSITIVE] Updated sensitive app_ids from sparrow.ini (%zu entries)",
        sensitive_app_ids.size());
    recompute_all_views_sensitivity();
}

void Core::set_sensitive_output_names(std::vector<std::string> output_names)
{
    sensitive_output_names = std::move(output_names);
    wlr_log(WLR_INFO, "[SENSITIVE] Updated sensitive output_names (%zu entries)",
        sensitive_output_names.size());
    sparrow_damage_add_box(nullptr);
}

bool Core::is_output_sensitive(const struct wlr_output *wlr_out) const
{
    if (!wlr_out || !wlr_out->name || (wlr_out->name[0] == '\0') ||
        sensitive_output_names.empty())
    {
        return false;
    }

    std::string out_name(wlr_out->name);
    std::transform(
        out_name.begin(), out_name.end(), out_name.begin(),
        [] (unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto & pattern : sensitive_output_names)
    {
        if (pattern.empty())
        {
            continue;
        }

        std::string needle = pattern;
        std::transform(
            needle.begin(), needle.end(), needle.begin(),
            [] (unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if ((out_name == needle) || (out_name.find(needle) != std::string::npos))
        {
            return true;
        }
    }

    return false;
}

bool sparrow_get_sensitive_view_buffer_box(Core *instance, SparrowView *view,
    struct wlr_output *wlr_out,
    struct wlr_box *out_buf_box,
    int *out_corner_radius_px)
{
    if (!instance || !view || !wlr_out || !out_buf_box)
    {
        return false;
    }

    if (!view->is_sensitive || !view->xdg_surface ||
        !view->xdg_surface->surface || !view->xdg_surface->surface->mapped)
    {
        return false;
    }

    if ((instance->engine != nullptr) && !sparrow_view_is_visible(view))
    {
        return false;
    }

    const bool active_or_transition =
        (instance->force_render_all_views || instance->gesture_active ||
            view->activated);

    if (view->has_flutter_scene_box && !view->scene_visible &&
        !active_or_transition)
    {
        return false;
    }

    struct wlr_box out_layout_box = {};
    if (instance->output_layout)
    {
        wlr_output_layout_get_box(instance->output_layout, wlr_out,
            &out_layout_box);
    }

    if ((out_layout_box.width <= 0) || (out_layout_box.height <= 0))
    {
        wlr_output_effective_resolution(wlr_out, &out_layout_box.width,
            &out_layout_box.height);
    }

    struct wlr_box scene_box = {};
    if (!sparrow_view_get_scene_box(view, &scene_box))
    {
        if ((instance->engine != nullptr) && !active_or_transition)
        {
            return false;
        }

        if (view->has_flutter_scene_box && (view->flutter_scene_box.width > 0) &&
            (view->flutter_scene_box.height > 0))
        {
            scene_box = view->flutter_scene_box;
            if (view->current_output && view->current_output->wlr_output &&
                instance->output_layout)
            {
                struct wlr_box out_geom = {};
                wlr_output_layout_get_box(instance->output_layout,
                    view->current_output->wlr_output, &out_geom);
                scene_box.x += out_geom.x;
                scene_box.y += out_geom.y;
            }
        } else
        {
            scene_box.x     = (view->x > 0) ? view->x : out_layout_box.x;
            scene_box.y     = (view->y > 0) ? view->y : out_layout_box.y;
            scene_box.width = (view->width > 0) ? view->width : out_layout_box.width;
            scene_box.height =
                (view->height > 0) ? view->height : out_layout_box.height;
        }
    }

    int trans_w = 0, trans_h = 0;
    wlr_output_transformed_resolution(wlr_out, &trans_w, &trans_h);
    if ((trans_w <= 0) || (trans_h <= 0))
    {
        trans_w = wlr_out->width;
        trans_h = wlr_out->height;
    }

    const double scale = (wlr_out->scale > 0.0f) ? wlr_out->scale : 1.0;
    const double rel_x = scene_box.x - out_layout_box.x;
    const double rel_y = scene_box.y - out_layout_box.y;

    const int bx1 =
        std::max(0, static_cast<int>(std::floor(rel_x * scale)) - 1);
    const int by1 =
        std::max(0, static_cast<int>(std::floor(rel_y * scale)) - 1);
    const int bx2 =
        std::min(trans_w,
            static_cast<int>(std::ceil((rel_x + scene_box.width) * scale)) + 1);
    const int by2 =
        std::min(trans_h,
            static_cast<int>(std::ceil((rel_y + scene_box.height) * scale)) + 1);

    if ((bx2 <= 0) || (by2 <= 0) || (bx1 >= trans_w) || (by1 >= trans_h) ||
        (bx2 <= bx1) || (by2 <= by1))
    {
        return false;
    }

    struct wlr_box local_box = {
        .x     = bx1,
        .y     = by1,
        .width = bx2 - bx1,
        .height = by2 - by1,
    };

    if (wlr_out->transform != WL_OUTPUT_TRANSFORM_NORMAL)
    {
        wlr_box_transform(&local_box, &local_box,
            wlr_output_transform_invert(wlr_out->transform), trans_w,
            trans_h);
    }

    if ((local_box.x + local_box.width <= 0) ||
        (local_box.y + local_box.height <= 0) ||
        (local_box.x >= wlr_out->width) || (local_box.y >= wlr_out->height) ||
        (local_box.width <= 0) || (local_box.height <= 0))
    {
        return false;
    }

    *out_buf_box = local_box;

    if (out_corner_radius_px != nullptr)
    {
        int r_px = 0;
        if (view->has_flutter_scene_box && (view->flutter_corner_radius > 0.5))
        {
            r_px = static_cast<int>(std::lround(view->flutter_corner_radius * scale));
        }

        *out_corner_radius_px = r_px;
    }

    return true;
}

void Core::dump_surface_tree() const
{
    if (!trace_surfacetree)
    {
        wlr_log(WLR_INFO, "[INSPECT] Surface tree inspector is disabled (enable "
                          "with --trace-surfacetree)");
        return;
    }

    fprintf(stderr, "\n\033[1;36m======================= SPARROW SURFACE TREE "
                    "=======================\033[0m\n");

    // Outputs
    Output *out = nullptr;
    wl_list_for_each(out, const_cast<struct wl_list*>(&outputs), link)
    {
        if (!out->wlr_output)
        {
            continue;
        }

        int refresh = out->wlr_output->current_mode ?
            (out->wlr_output->current_mode->refresh / 1000) :
            60;
        struct wlr_box out_box = {0, 0, 0, 0};
        if (output_layout)
        {
            wlr_output_layout_get_box(output_layout, out->wlr_output, &out_box);
        }

        fprintf(stderr,
            "\033[1;32m[Output]\033[0m %s (id=%x) [%dx%d @ %dHz] Scale: %.2f | "
            "Pos: (%d, %d) | %s\n",
            out->wlr_output->name, out->id, out->wlr_output->width,
            out->wlr_output->height, refresh, out->wlr_output->scale, out_box.x,
            out_box.y, (out == vsync_output) ? "VSync Primary" : "Secondary");
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
            struct wl_client *client =
                wl_resource_get_client(view->xdg_surface->resource);
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
                buffer_type = view->xdg_surface->surface->buffer->texture ?
                    "Hardware/Texture" :
                    "ClientBuffer";
            }
        }

        fprintf(stderr,
            " \033[1;34m├─ [View #%x]\033[0m PID: %d | App: \"%s\" | Title: "
            "\"%s\"\n",
            view->handle, pid, app_id, title);
        fprintf(stderr,
            " │  ├─ Geometry: %dx%d @ (%d, %d) | Buffer: %s (%dx%d) | Focused: "
            "%s | Mapped: %s\n",
            view->width, view->height, view->x, view->y, buffer_type, buf_w,
            buf_h, view->activated ? "YES" : "NO",
            (view->xdg_surface && view->xdg_surface->surface &&
                view->xdg_surface->surface->mapped) ?
            "YES" :
            "NO");

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
                        sub_buf_type = sub->surface->buffer->texture ? "Hardware/Texture" :
                            "ClientBuffer";
                    }

                    fprintf(stderr,
                        " │  ├─ \033[1;33m[Sub #%x]\033[0m %dx%d @ (%d, %d) | "
                        "Buffer: %s (%dx%d) | TexID: %ld\n",
                        sub->handle, sub->width, sub->height, sub->x, sub->y,
                        sub_buf_type, sub->buffer_width, sub->buffer_height,
                        sub->texture_id);
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
                        " │  └─ \033[1;35m[Popup #%x]\033[0m %dx%d @ (%d, %d) | "
                        "Mapped: %s | Unconstrained: %s\n",
                        pop->handle, pop->width, pop->height, pop->x, pop->y,
                        (pop->xdg_surface && pop->xdg_surface->surface &&
                            pop->xdg_surface->surface->mapped) ?
                        "YES" :
                        "NO",
                        pop->unconstrained ? "YES" : "NO");
                }
            });
        }
    }

    if (view_count == 0)
    {
        fprintf(stderr, " \033[0;90m(No active toplevel views)\033[0m\n");
    }

    fprintf(stderr, "\033[1;36m=================================================="
                    "==================\033[0m\n\n");
}

int Core::init(const sparrow_options & opts, bool allow_root)
{
    const auto core = this;

    const char *dbg_gpureset_env = getenv("SPARROW_DEBUG_GPURESET");
    if (dbg_gpureset_env != nullptr)
    {
        core->debug_gpureset = (strcmp(dbg_gpureset_env, "1") == 0 ||
            strcasecmp(dbg_gpureset_env, "true") == 0);
    }

    const char *trace_surfacetree_env = getenv("SPARROW_TRACE_SURFACETREE");
    if (trace_surfacetree_env != nullptr)
    {
        core->trace_surfacetree = (strcmp(trace_surfacetree_env, "1") == 0 ||
            strcasecmp(trace_surfacetree_env, "true") == 0);
    }

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
        if ((strcasecmp(buffering_env, "double") == 0) ||
            (strcmp(buffering_env, "0") == 0))
        {
            core->buffering_mode = Core::BUFFERING_DOUBLE;
        } else if ((strcasecmp(buffering_env, "triple") == 0) ||
                   (strcmp(buffering_env, "2") == 0) ||
                   (strcasecmp(buffering_env, "on") == 0))
        {
            core->buffering_mode = Core::BUFFERING_TRIPLE;
        } else if ((strcasecmp(buffering_env, "auto") == 0) ||
                   (strcmp(buffering_env, "1") == 0) ||
                   (strcasecmp(buffering_env, "dynamic") == 0))
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
    core->main_thread_id = std::this_thread::get_id();

    wlr_log(WLR_INFO, "Starting sparrow: %s", get_version_string().c_str());
    const char *buf_mode_name = (core->buffering_mode == Core::BUFFERING_DOUBLE) ?
        "DOUBLE BUFFERING (DB)" :
        (core->buffering_mode == Core::BUFFERING_AUTO) ?
        "DYNAMIC TRIPLE BUFFERING (AUTO)" :
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

    core->backend = wlr_backend_autocreate(core->wl_event_loop, &core->session);
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
        const auto instance = static_cast<Core*>(data);
        return (int)instance->callable_queue.execute();
    };

    core->callable_queue_event_source =
        wl_event_loop_add_fd(core->wl_event_loop, core->callable_queue.get_fd(),
            WL_EVENT_READABLE, callable_queue_function, core);

    core->fps_decay_timer = wl_event_loop_add_timer(
        core->wl_event_loop,
        [] (void *data) -> int
    {
        const auto inst = static_cast<Core*>(data);
        if (inst && inst->show_fps)
        {
            sparrow_damage_add_box(nullptr);
        }

        return 0;
    },
        core);

    core->pointer_bottom_dwell_timer = wl_event_loop_add_timer(
        core->wl_event_loop,
        [] (void *data) -> int
    {
        const auto inst = static_cast<Core*>(data);
        if (!inst)
        {
            return 0;
        }

        if (inst->pointer_bottom_gesture_armed &&
            inst->pointer_in_bottom_edge && inst->immersive_sticky &&
            (inst->active_constraint == nullptr) &&
            (inst->direct_input_surface == 0))
        {
            bool has_views = !wl_list_empty(&inst->views_list) &&
                !inst->force_render_all_views;

            if (has_views && inst->pigeon_flutter_api)
            {
                wlr_log(WLR_INFO,
                    "[DOCK] Pointer bottom dwell timer fired, revealing dock");
                inst->immersive_sticky = false;
                inst->pointer_bottom_gesture_armed = false;
                inst->pointer_in_bottom_edge = false;
                inst->sparrow_renderer.current_scene.needs_update = true;
                inst->pigeon_flutter_api->DockReveal(
                    [] () {}, [] (const sparrow::FlutterError &) {});
                sparrow_damage_add_box(nullptr);
            }
        }

        return 0;
    },
        core);

    core->scanout_settle_timer = wl_event_loop_add_timer(
        core->wl_event_loop,
        [] (void *data) -> int
    {
        // Settling delay expired: trigger frame update to transition to direct
        // scanout
        sparrow_damage_add_box(nullptr, false);
        return 0;
    },
        core);

    int core_drm_fd  = -1;
    char *drm_device = getenv("WLR_RENDER_DRM_DEVICE");
    if (drm_device)
    {
        core_drm_fd = open(drm_device, O_RDWR | O_CLOEXEC);
    } else
    {
        core_drm_fd = wlr_backend_get_drm_fd(core->backend);
    }

    if (core_drm_fd < 0)
    {
        // When running with a non-DRM backend (e.g. headless for bot/PGO, X11, or
        // Wayland nested), probe for an available hardware DRM render node to
        // enable GPU acceleration instead of falling back to buggy software
        // rendering (llvmpipe).
        for (int i = 128; i < 136; ++i)
        {
            char node_path[32];
            snprintf(node_path, sizeof(node_path), "/dev/dri/renderD%d", i);
            int fd = open(node_path, O_RDWR | O_CLOEXEC);
            if (fd >= 0)
            {
                core_drm_fd = fd;
                wlr_log(
                    WLR_INFO,
                    "Auto-detected DRM render node for GPU acceleration: %s (fd=%d)",
                    node_path, core_drm_fd);
                break;
            }
        }
    }

    core->drm_fd = core_drm_fd;

    if (core_drm_fd < 0)
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
        return EXIT_FAILURE;
#endif
    }

    wlr_egl *core_egl = wlr_egl_create_with_drm_fd(core_drm_fd);

    if (core_egl == nullptr)
    {
        wlr_log(WLR_ERROR, "Failed to create EGL");
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    core->egl = core_egl;

    wlr_renderer *core_renderer = wlr_gles2_renderer_create(core_egl);
    if (core_renderer == nullptr)
    {
        wlr_log(WLR_ERROR, "Failed to create GLES2 renderer");
        wlr_egl_destroy(core_egl);
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    core->renderer = core_renderer;
    core->renderer_lost.notify = handle_renderer_lost;
    wl_signal_add(&core->renderer->events.lost, &core->renderer_lost);

    // wlr_egl is opaque in 0.18 - extract EGL display/context via accessors
    core->egl_display = wlr_egl_get_display(core_egl);
    core->egl_context = wlr_egl_get_context(core_egl);

    // NOTE: wp_linux_drm_syncobj_manager_v1 (explicit sync) must NOT be
    // advertised until Flutter's external texture pipeline can wait on client
    // acquire timeline points and signal release timeline points. Advertising
    // this protocol causes Mesa Vulkan WSI (e.g. mpv gpu-next) to disable
    // implicit kernel sync and submit acquire/release timeline points that
    // Sparrow never waits on or signals, leading to severe tearing and
    // flickering. By keeping this disabled, clients fall back to standard DMA-BUF
    // implicit sync fences where the Linux DRM kernel driver automatically
    // synchronizes reader and writer access.
#if 0
    if ((wlr_renderer_get_drm_fd(core->renderer) >= 0) &&
        core->renderer->features.timeline)
    {
        wlr_linux_drm_syncobj_manager_v1_create(
            core->wl_display, 1, wlr_renderer_get_drm_fd(core->renderer));
    }

#endif

    core->allocator = wlr_allocator_autocreate(core->backend, core->renderer);
    if (core->allocator == nullptr)
    {
        wlr_log(WLR_ERROR, "Failed to create allocator: neither DRM render node "
                           "(/dev/dri) nor /dev/udmabuf is available");
        wlr_egl_destroy(core_egl);
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    if (!allow_root && !drop_permissions())
    {
        wlr_egl_destroy(core_egl);
        wl_display_destroy_clients(core->wl_display);
        wl_display_destroy(core->wl_display);
        delete (core);
        return EXIT_FAILURE;
    }

    wlr_renderer_init_wl_display(core->renderer, core->wl_display);

    core->compositor = wlr_compositor_create(core->wl_display, 6, core->renderer);
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
    wl_list_init(&core->session_lock_surfaces);
    wl_list_init(&core->idle_inhibitors);
    wl_list_init(&core->new_session_lock.link);
    wl_list_init(&core->new_idle_inhibitor.link);
    core->vsync_output     = nullptr;
    core->next_output_id   = 0;
    core->vsync_rate_limit = 0;

    wlr_color_representation_manager_v1_create_with_renderer(core->wl_display, 1,
        core->renderer);

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
    wlr_xdg_output_manager_v1_create(core->wl_display, core->output_layout);

    // Output manager protocol - dynamic output configuration (wlr-randr,
    // wdisplays, kanshi)
    sparrow_output_manager_init();
    sparrow_output_power_manager_init();

    core->xdg_shell = wlr_xdg_shell_create(core->wl_display, 5);
    core->new_xdg_toplevel.notify = sparrow_new_xdg_toplevel;
    wl_signal_add(&core->xdg_shell->events.new_toplevel, &core->new_xdg_toplevel);

    wlr_tablet_v2_create(core->wl_display);

    // Screencopy - enables screenshots (grim) and screen recording with sensitive
    // content censoring
    core->screencopy_manager =
        sparrow_screencopy_manager_v1_create(core->wl_display);

    // Export DMA-BUF - enables screen sharing (OBS, Discord, Zoom) with sensitive
    // content censoring
    core->export_dmabuf_manager =
        sparrow_export_dmabuf_manager_v1_create(core->wl_display);

    // Secure output protocol - allows clients to mark surfaces as sensitive
    core->secure_output_manager = sparrow_secure_output_init(core->wl_display);

    wlr_alpha_modifier_v1_create(core->wl_display);

    // Idle notification - lets apps know when user is idle (for screen lock,
    // power management)
    core->idle_notifier = wlr_idle_notifier_v1_create(core->wl_display);

    // Idle inhibit - allows apps to prevent idle (video playback, presentations)
    sparrow_idle_inhibit_init(core);

    // ext-session-lock-v1 - secure screen locking and full input/output blanking
    sparrow_session_lock_init(core);

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
    core->new_toplevel_decoration.notify = sparrow_handle_new_toplevel_decoration;
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

    core->pointer_gestures = wlr_pointer_gestures_v1_create(core->wl_display);
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
        wl_signal_add(&core->virtual_keyboard_manager->events.new_virtual_keyboard,
            &core->new_virtual_keyboard);
        wlr_log(WLR_INFO,
            "Enabled virtual keyboard protocol (wlr_virtual_keyboard_v1)");
    }

    core->virtual_pointer_manager =
        wlr_virtual_pointer_manager_v1_create(core->wl_display);
    if (core->virtual_pointer_manager != nullptr)
    {
        core->new_virtual_pointer.notify = handle_new_virtual_pointer;
        wl_signal_add(&core->virtual_pointer_manager->events.new_virtual_pointer,
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

    core->process_supervisor = new ProcessSupervisor(core);

    increase_nofile_limit();

    // Handle popup surfaces (menus, dropdowns, tooltips)
    core->new_xdg_popup.notify = sparrow_new_xdg_popup;
    wl_signal_add(&core->xdg_shell->events.new_popup, &core->new_xdg_popup);

    auto socket = choose_socket(core->wl_display);
    if (!socket)
    {
        wlr_log(WLR_ERROR, "Failed to create Wayland socket");
        wlr_egl_destroy(core_egl);
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
        wlr_egl_destroy(core_egl);
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
    core->sigint_event_source =
        wl_event_loop_add_signal(event_loop, SIGINT, handle_term_signal, core);
    core->sigterm_event_source =
        wl_event_loop_add_signal(event_loop, SIGTERM, handle_term_signal, core);
    core->sigusr2_event_source = wl_event_loop_add_signal(
        event_loop, SIGUSR2, handle_gpu_reset_signal, core);

    core->flutter_renderer_config.type = kOpenGL;
    core->flutter_renderer_config.open_gl.struct_size =
        sizeof(FlutterOpenGLRendererConfig);
    core->flutter_renderer_config.open_gl.make_current =
        engine_cb_renderer_make_current;
    core->flutter_renderer_config.open_gl.clear_current =
        engine_cb_renderer_clear_current;
    core->flutter_renderer_config.open_gl.make_resource_current =
        engine_cb_renderer_make_resource_current;
    core->flutter_renderer_config.open_gl.fbo_reset_after_present = true;
    core->flutter_renderer_config.open_gl.gl_proc_resolver =
        engine_cb_renderer_gl_proc_resolve;
    core->flutter_renderer_config.open_gl.gl_external_texture_frame_callback =
        engine_cb_external_texture;
    core->flutter_renderer_config.open_gl.fbo_with_frame_info_callback =
        engine_cb_renderer_fbo;
    core->flutter_renderer_config.open_gl.present_with_info =
        engine_cb_renderer_present;

    core->flutter_project_args.struct_size = sizeof(FlutterProjectArgs);
    core->flutter_project_args.command_line_argc = opts.argc;
    core->flutter_project_args.command_line_argv = opts.argv;
    core->flutter_project_args.assets_path   = opts.assets_path.c_str();
    core->flutter_project_args.icu_data_path = opts.icu_data_path.c_str();
    core->flutter_project_args.platform_message_callback =
        engine_cb_platform_message;
    core->flutter_project_args.log_message_callback = engine_cb_log_message;
    core->flutter_project_args.custom_task_runners  = &core->custom_task_runners;
    if (opts.argc > 1)
    {
        core->flutter_project_args.dart_entrypoint_argc = opts.argc - 1;
        core->flutter_project_args.dart_entrypoint_argv = opts.argv + 1;
    }

#ifdef FLUTTER_COMPOSITOR
    core->flutter_project_args.compositor = &core->fl_compositor;
#endif
    core->flutter_project_args.vsync_callback = sparrow_engine_vsync_callback;
    core->flutter_project_args.shutdown_dart_vm_when_done = false;
    core->flutter_project_args.engine_id = reinterpret_cast<int64_t>(core);

    if (core->embedder_api.RunsAOTCompiledDartCode())
    {
        FlutterEngineAOTDataSource aot_source = {};

        aot_source    = {
            .type     = kFlutterEngineAOTDataSourceTypeElfPath,
            .elf_path = opts.elf_file_path.c_str(),
        };

        const FlutterEngineResult engine_result =
            FlutterEngineCreateAOTData(&aot_source, &core->aot_data);
        if (engine_result != kSuccess)
        {
            wlr_log(WLR_ERROR,
                "Could not load AOT data. FlutterEngineCreateAOTData failed.");
            wlr_egl_destroy(core_egl);
            wl_display_destroy_clients(core->wl_display);
            wl_display_destroy(core->wl_display);
            delete (core);
            return EXIT_FAILURE;
        }

        core->flutter_project_args.aot_data = core->aot_data;
    }

    // Initialize the flutter:: client wrapper messaging infrastructure
    core->message_dispatcher =
        std::make_unique<IncomingMessageDispatcher>(&core->messenger);
    core->messenger.SetMessageDispatcher(core->message_dispatcher.get());

    sparrow_text_input_init();
    sparrow_cursor_init();
    sparrow_keyboard_channel_init();
    sparrow_isolate_channel_init();
    // Cold boot: shell state (page order, surface handles, restoration data)
    // is only meaningful across an in-process soft GPU reset. Surface handles
    // restart at 1 in a new process, so stale persisted handles from a
    // previous run would corrupt the PageView ordering.
    sparrow_restoration_clear_persisted_state();
    sparrow_session_manifest_init();
    sparrow_engine_init_channels();

    const FlutterEngineResult fl_result = core->embedder_api.Run(
        FLUTTER_ENGINE_VERSION, &core->flutter_renderer_config,
        &core->flutter_project_args, (void*)core, &core->engine);

    if (fl_result != kSuccess)
    {
        wlr_log(WLR_ERROR, "Flutter Engine Run failed!");
    }

    // Connect the BinaryMessenger to the running engine (must be after Run)
    core->messenger.SetEngine(core->engine, &core->embedder_api);

    // Send existing outputs to Flutter so that initial displays are immediately
    // available
    sparrow_send_all_outputs();

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
        core->embedder_api.SendWindowMetricsEvent(core->engine, &window_metrics);
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

    if (core->process_supervisor)
    {
        core->process_supervisor->init(event_loop);
    }

    wl_display_run(core->wl_display);
    if (exit_because_signal == SIGINT)
    {
        wlr_log(WLR_INFO, "Got SIGINT, shutting down");
    } else if (exit_because_signal == SIGTERM)
    {
        wlr_log(WLR_INFO, "Got SIGTERM, shutting down");
    }

    if (core->process_supervisor)
    {
        core->process_supervisor->shutdown();
    }

    sparrow_restoration_flush_to_disk();
    sparrow_session_manifest_flush();

    if (core->engine != nullptr)
    {
        intptr_t old_baton = atomic_exchange(&core->vsync_baton, 0);
        if (old_baton != 0)
        {
            uint64_t current_time = core->embedder_api.GetCurrentTime();
            core->embedder_api.OnVsync(core->engine, old_baton, current_time,
                current_time + 16600000);
        }
    }

    {
        std::scoped_lock lock(core->platform_task_mutex);
        core->queued_platform_tasks.clear();
    }

    sparrow_engine_reset_channels();
    core->messenger.Shutdown();
    core->messenger.SetEngine(nullptr, nullptr);

    wl_display_destroy_clients(core->wl_display);
    engine_dispose(core->engine, core->flutter_project_args.aot_data);
    core->messenger.SetMessageDispatcher(nullptr);
    core->message_dispatcher.reset();

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
        (core->output_manager_apply.link.next !=
         &core->output_manager_apply.link))
    {
        wl_list_remove(&core->output_manager_apply.link);
        wl_list_init(&core->output_manager_apply.link);
    }

    if ((core->output_manager_test.link.prev != nullptr) &&
        (core->output_manager_test.link.next !=
         &core->output_manager_test.link))
    {
        wl_list_remove(&core->output_manager_test.link);
        wl_list_init(&core->output_manager_test.link);
    }

    if ((core->output_power_manager_set_mode.link.prev != nullptr) &&
        (core->output_power_manager_set_mode.link.next !=
         &core->output_power_manager_set_mode.link))
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

    sparrow_seat_finish();
    sparrow_renderer_destroy();
    sparrow_udmabuf_finish();
    sparrow_dispatcher_finish();

    Output *out, *out_tmp;
    wl_list_for_each_safe(out, out_tmp, &core->outputs, link)
    {
        if ((out->destroy.link.prev != nullptr) &&
            (out->destroy.link.next != nullptr))
        {
            wl_list_remove(&out->destroy.link);
            wl_list_init(&out->destroy.link);
        }

        if ((out->frame.link.prev != nullptr) &&
            (out->frame.link.next != nullptr))
        {
            wl_list_remove(&out->frame.link);
            wl_list_init(&out->frame.link);
        }

        if ((out->request_state.link.prev != nullptr) &&
            (out->request_state.link.next != nullptr))
        {
            wl_list_remove(&out->request_state.link);
            wl_list_init(&out->request_state.link);
        }

        if ((out->present.link.prev != nullptr) &&
            (out->present.link.next != nullptr))
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

    if (core->recreating_renderer != nullptr)
    {
        wl_event_source_remove(core->recreating_renderer);
        core->recreating_renderer = nullptr;
    }

    if ((core->renderer_lost.link.prev != nullptr) &&
        (core->renderer_lost.link.next != nullptr) &&
        (core->renderer_lost.link.next != &core->renderer_lost.link))
    {
        wl_list_remove(&core->renderer_lost.link);
        wl_list_init(&core->renderer_lost.link);
    }

    if (core->xcursor_texture != nullptr)
    {
        wlr_texture_destroy(core->xcursor_texture);
        core->xcursor_texture = nullptr;
    }

    if (core->renderer != nullptr)
    {
        wlr_renderer_destroy(core->renderer);
        core->renderer = nullptr;
    }

    if (core->egl_display != EGL_NO_DISPLAY)
    {
        eglTerminate(core->egl_display);
        eglReleaseThread();
        core->egl_display = EGL_NO_DISPLAY;
        core->egl_context = EGL_NO_CONTEXT;
        core->egl = nullptr;
    }

    if ((core->drm_fd >= 0) &&
        ((core->backend == nullptr) ||
         (core->drm_fd != wlr_backend_get_drm_fd(core->backend))))
    {
        close(core->drm_fd);
        core->drm_fd = -1;
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

    if (core->sigusr2_event_source != nullptr)
    {
        wl_event_source_remove(core->sigusr2_event_source);
        core->sigusr2_event_source = nullptr;
    }

    if (core->fps_decay_timer != nullptr)
    {
        wl_event_source_remove(core->fps_decay_timer);
        core->fps_decay_timer = nullptr;
    }

    if (core->pointer_bottom_dwell_timer != nullptr)
    {
        wl_event_source_remove(core->pointer_bottom_dwell_timer);
        core->pointer_bottom_dwell_timer = nullptr;
    }

    if (core->scanout_settle_timer != nullptr)
    {
        wl_event_source_remove(core->scanout_settle_timer);
        core->scanout_settle_timer = nullptr;
    }

    struct wl_display *wl_display = core->wl_display;
    delete (core);
    wl_display_destroy(wl_display);
    wlr_log(WLR_INFO, "Shutdown successful!");
    return EXIT_SUCCESS;
}
