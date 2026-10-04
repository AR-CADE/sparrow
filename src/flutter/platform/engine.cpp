#include <EGL/egl.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <mutex>
#ifdef USE_GLES32
    #include <GLES3/gl32.h>
#else
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#endif

#include <sched.h>
#include <sparrow/nonstd/wlroots-full.hpp>

#include "client_wrapper/encodable_value.h"
#include "client_wrapper/method_channel.h"
#include "client_wrapper/standard_method_codec.h"
#include "core.hpp"
#include "cursor.hpp"
#include "engine.hpp"
#include "engine/callbacks/seat_callback.hpp"
#include "engine/callbacks/surface_callback.hpp"
#include "engine/messages/output_message.hpp"
#include "engine/messages/popup_message.hpp"
#include "input/pointer.hpp"
#include "ipc/ipc_server.hpp"
#include "output.hpp"
#include "surface/popup.hpp"
#include "surface/sub_surface.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"
#include "util/handle_map.hpp"
#include "util/udmabuf.hpp"
#include <flutter/platform/pigeon/messages.h>

void engine_dispose(FlutterEngine engine, FlutterEngineAOTData aot_data)
{
    Core *instance = Core::instance();

    if (engine != nullptr)
    {
        FlutterEngine eng = instance->engine;
        instance->engine = nullptr;
        if (instance->embedder_api.Shutdown(eng) != kSuccess)
        {
            wlr_log(WLR_ERROR, "Failed to shutdown Flutter engine");
        }
    }

    if (aot_data != nullptr)
    {
        if (instance->embedder_api.CollectAOTData(aot_data) != kSuccess)
        {
            wlr_log(WLR_ERROR, "Failed to send collect AOT data");
        }
    }
}

bool engine_cb_renderer_make_current(void *user_data)
{
    Core *instance = Core::instance();

    eglMakeCurrent(instance->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
        instance->sparrow_renderer.flutter_egl_context);

    return true;
}

bool engine_cb_renderer_clear_current(void *user_data)
{
    Core *instance = Core::instance();

    eglMakeCurrent(instance->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
        EGL_NO_CONTEXT);

    return true;
}

bool engine_cb_renderer_make_resource_current(void *user_data)
{
    Core *instance = Core::instance();
    eglMakeCurrent(instance->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
        instance->sparrow_renderer.flutter_resource_egl_context);
    return true;
}

void *engine_cb_renderer_gl_proc_resolve(void *user_data, const char *name)
{
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

// Helper function to provide texture for a wlr_surface
static bool is_surface_fully_opaque(struct wlr_surface *surface)
{
    if (!surface)
    {
        return false;
    }

    if (surface->buffer && surface->buffer->source)
    {
        if (wlr_buffer_is_opaque(surface->buffer->source))
        {
            return true;
        }
    }

    if ((surface->current.width > 0) && (surface->current.height > 0))
    {
        pixman_box32_t box = {
            .x1 = 0,
            .y1 = 0,
            .x2 = surface->current.width,
            .y2 = surface->current.height,
        };
        if (pixman_region32_contains_rectangle(&surface->opaque_region, &box) ==
            PIXMAN_REGION_IN)
        {
            return true;
        }
    }

    return false;
}

static void apply_texture_opacity_swizzle(const FlutterOpenGLTexture *texture,
    bool is_opaque)
{
#ifdef USE_GLES32
    if (texture && (texture->target == GL_TEXTURE_2D) && (texture->name != 0))
    {
        glBindTexture(texture->target, texture->name);
        glTexParameteri(texture->target, GL_TEXTURE_SWIZZLE_A,
            is_opaque ? GL_ONE : GL_ALPHA);
        glBindTexture(texture->target, 0);
    }

#else
    (void)texture;
    (void)is_opaque;
#endif
}

//
// Clean separation: Once buffer type is detected, we use only that path.
// This avoids overhead from repeatedly trying DMA-BUF for SHM surfaces.
// Helper to provide texture for Flutter external texture (platform views)
bool provide_surface_texture(struct wlr_surface *surface,
    size_t requested_width, size_t requested_height,
    FlutterOpenGLTexture *texture_out,
    bool force_opaque)
{
    if (!surface || !texture_out)
    {
        return false;
    }

    const bool opaque = force_opaque || is_surface_fully_opaque(surface);

#ifdef USE_DMABUF
    if (sparrow_renderer_import_surface_dmabuf(surface, texture_out))
    {
        apply_texture_opacity_swizzle(texture_out, opaque);
        return true;
    }

#endif

    struct wlr_texture *wlr_tex = sparrow_surface_get_texture(surface);
    if (wlr_tex == nullptr)
    {
        return false;
    }

    struct wlr_gles2_texture_attribs attribs;
    wlr_gles2_texture_get_attribs(wlr_tex, &attribs);

    const size_t width = surface->current.buffer_width ?
        surface->current.buffer_width :
        surface->current.width;
    const size_t height = surface->current.buffer_height ?
        surface->current.buffer_height :
        surface->current.height;

    if ((width == 0) || (height == 0))
    {
        return false;
    }

    texture_out->target = attribs.target;
    texture_out->name   = attribs.tex;
#ifdef USE_GLES32
    texture_out->format = GL_RGBA8;
#else
    texture_out->format = GL_RGBA8_OES;
#endif
    texture_out->width     = width;
    texture_out->height    = height;
    texture_out->user_data = nullptr;
    texture_out->destruction_callback = nullptr;

    apply_texture_opacity_swizzle(texture_out, opaque);

    return true;
}

bool engine_cb_external_texture(void *user_data, int64_t texture_id,
    size_t width, size_t height,
    FlutterOpenGLTexture *texture_out)
{
    bool result    = false;
    Core *instance = Core::instance();
    if (!instance)
    {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(instance->sparrow_renderer.texture_mutex);

    // First, try to find a view (toplevel surface)
    if (!wl_list_empty(&instance->views_list))
    {
        SparrowView *view = nullptr;
        SparrowView *v    = nullptr;
        wl_list_for_each(v, &instance->views_list, link)
        {
            if (v->handle == (uint32_t)texture_id)
            {
                view = v;
                break;
            }
        }

        if (view != nullptr)
        {
            struct timespec ts_sample;
            clock_gettime(CLOCK_MONOTONIC, &ts_sample);
            uint64_t sample_now_us =
                (uint64_t)ts_sample.tv_sec * 1000000ULL + (ts_sample.tv_nsec / 1000);
            double sample_dt_ms =
                (view->last_sample_time_us > 0) ?
                (double)(sample_now_us - view->last_sample_time_us) / 1000.0 :
                0.0;
            view->last_sample_time_us = sample_now_us;

            if (view->locked_buffer != nullptr)
            {
                if (view->locked_buffer == view->last_sampled_buffer)
                {
                    view->duplicate_sample_count++;
                    if (instance->debug_pacing)
                    {
                        const char *app = (view->toplevel && view->toplevel->app_id) ?
                            view->toplevel->app_id :
                            "unknown";
                        wlr_log(WLR_INFO,
                            "[PACING-GHOST] Flutter re-sampled same buffer %p for "
                            "'%s'! dt=%.2fms (ghosts: %lu, total samples: %lu)",
                            (void*)view->locked_buffer, app, sample_dt_ms,
                            (unsigned long)view->duplicate_sample_count,
                            (unsigned long)view->sampled_count);
                    }
                } else
                {
                    view->last_sampled_buffer    = view->locked_buffer;
                    view->current_buffer_sampled = true;
                    view->sampled_count++;
                }
            }

#ifdef USE_DMABUF
            if (view->locked_buffer != nullptr)
            {
                if (sparrow_renderer_import_dmabuf_buffer(view->locked_buffer,
                    texture_out))
                {
                    const bool opaque = is_surface_fully_opaque(
                        view->xdg_surface ? view->xdg_surface->surface : nullptr);
                    apply_texture_opacity_swizzle(texture_out, opaque);
                    return true;
                }
            }

#endif
            if ((view->xdg_surface == nullptr) ||
                (view->xdg_surface->surface == nullptr))
            {
                wlr_log(WLR_DEBUG, "texture_id=%ld: view surface nullptr", texture_id);
                return false;
            }

            // Pass Flutter's requested dimensions for resize smoothing
            result = provide_surface_texture(view->xdg_surface->surface, width,
                height, texture_out, false);
            if (!result)
            {
                wlr_log(WLR_DEBUG,
                    "texture_id=%ld: provide_surface_texture returned false",
                    texture_id);
            }

            return result;
        }
    }

    // If not a view, try to find a subsurface
    // Subsurface texture IDs are offset by 100000 to avoid collision with view
    // IDs
    SparrowSubSurface *sub = nullptr;
    if ((texture_id >= 100000) && (texture_id < 200000))
    {
        const uint32_t sub_handle = (uint32_t)(texture_id - 100000);
        if (handle_map_get(instance->subsurfaces, sub_handle,
            reinterpret_cast<void**>(&sub)))
        {
#ifdef USE_DMABUF
            if (sub->locked_buffer != nullptr)
            {
                if (sparrow_renderer_import_dmabuf_buffer(sub->locked_buffer,
                    texture_out))
                {
                    const bool opaque = is_surface_fully_opaque(sub->surface);
                    apply_texture_opacity_swizzle(texture_out, opaque);
                    return true;
                }
            }

#endif
            if (sub->surface == nullptr)
            {
                return false;
            }

            // Subsurfaces use actual size (no resize smoothing needed)
            result = provide_surface_texture(sub->surface, 0, 0, texture_out, false);
            return result;
        }
    }

    // If not a subsurface, try to find a popup
    // Popup texture IDs are offset by 200000 to avoid collision
    SparrowPopup *popup = nullptr;
    if (texture_id >= 200000)
    {
        const uint32_t popup_handle = (uint32_t)(texture_id - 200000);
        if (handle_map_get(instance->popups, popup_handle,
            reinterpret_cast<void**>(&popup)))
        {
            wlr_log(WLR_DEBUG,
                "[POPUP-EXT-TEX] texture_id=%ld popup=%p (%dx%d)",
                texture_id, (void*)popup, popup->width, popup->height);

            if ((popup->xdg_surface == nullptr) ||
                (popup->xdg_surface->surface == nullptr))
            {
                return false;
            }

            struct wlr_surface *surf = popup->xdg_surface->surface;

            // Check for subsurfaces - Firefox/browsers render popup content to
            // subsurfaces
            struct wlr_subsurface *first_subsurface = nullptr;
            struct wlr_subsurface *subsurface;
            wl_list_for_each(subsurface, &surf->current.subsurfaces_below,
                current.link)
            {
                if (first_subsurface == nullptr)
                {
                    first_subsurface = subsurface;
                }
            }
            wl_list_for_each(subsurface, &surf->current.subsurfaces_above,
                current.link)
            {
                if (first_subsurface == nullptr)
                {
                    first_subsurface = subsurface;
                }
            }

            // Use subsurface if available (Firefox renders content there)
            struct wlr_surface *content_surface = surf;
            if ((first_subsurface != nullptr) &&
                (first_subsurface->surface != nullptr))
            {
                content_surface = first_subsurface->surface;
            }

            // Primary popup path: wlroots texture with opacity swizzle
            {
                struct wlr_texture *wlr_tex =
                    sparrow_surface_get_texture(content_surface);
                if (wlr_tex == nullptr)
                {
                    return false;
                }

                struct wlr_gles2_texture_attribs attribs;
                wlr_gles2_texture_get_attribs(wlr_tex, &attribs);

                const size_t tex_width = content_surface->current.buffer_width ?
                    content_surface->current.buffer_width :
                    content_surface->current.width;
                const size_t tex_height = content_surface->current.buffer_height ?
                    content_surface->current.buffer_height :
                    content_surface->current.height;

                if ((tex_width == 0) || (tex_height == 0))
                {
                    return false;
                }

                texture_out->target = attribs.target;
                texture_out->name   = attribs.tex;
#ifdef USE_GLES32
                texture_out->format = GL_RGBA8;
#else
                texture_out->format = GL_RGBA8_OES;
#endif
                texture_out->width     = tex_width;
                texture_out->height    = tex_height;
                texture_out->user_data = nullptr;
                texture_out->destruction_callback = nullptr;

                const bool opaque = is_surface_fully_opaque(content_surface);
                apply_texture_opacity_swizzle(texture_out, opaque);

                result = true;
            }

            // If content surface dimensions changed and differ from Flutter widget
            // size, queue a map update to notify Flutter
            if ((content_surface->current.width > 0) &&
                (content_surface->current.height > 0) &&
                ((content_surface->current.width != popup->width) ||
                 (content_surface->current.height != popup->height)))
            {
                instance->callable_queue.enqueue([handle = popup->handle]
                {
                    Core *inst = Core::instance();
                    if (!inst)
                    {
                        return;
                    }

                    SparrowPopup *p = nullptr;
                    if (handle_map_get(inst->popups, handle,
                        reinterpret_cast<void**>(&p)) &&
                        p)
                    {
                        send_popup_map(p);
                    }
                });
            }

            // Keep requesting updates to catch subsurface content changes
            if (result && popup->texture_registered)
            {
                instance->embedder_api.MarkExternalTextureFrameAvailable(
                    instance->engine, popup->texture_id);
            }

            return result;
        }
    }

    return false;
}

uint32_t engine_cb_renderer_fbo(void *user_data,
    const FlutterFrameInfo *frame_info)
{
#ifdef FLUTTER_COMPOSITOR
    return 0;
#else // FLUTTER_COMPOSITOR
    sparrow_instance *instance = static_cast<sparrow_instance*>(user_data);

    GLuint fbo = sparrow_renderer_get_active_fbo(instance);
    wlr_log(WLR_INFO, "==== FRAME EVENT ==== Engine given fbo: %d", fbo);

    return fbo;
#endif // FLUTTER_COMPOSITOR
}

bool engine_cb_renderer_present(void *user_data,
    const FlutterPresentInfo *present_info)
{
#ifdef FLUTTER_COMPOSITOR
    return false;
#else // FLUTTER_COMPOSITOR
    sparrow_instance *instance = static_cast<sparrow_instance*>(user_data);
    wlr_log(WLR_INFO, "==== FRAME EVENT ==== Engine called present with FBO: %d",
        present_info->fbo_id);

    sparrow_renderer_flip_fbo(instance);

    return true;
#endif // FLUTTER_COMPOSITOR
}

void sparrow_engine_init_channels()
{
    Core *instance = Core::instance();

    class SparrowCompositorHostApi : public sparrow::CompositorHostApi
    {
      public:
        void SurfaceRequestResize(
            int64_t handle, int64_t width, int64_t height, int64_t request_id,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            sparrow_handle_surface_request_resize((uint32_t)handle, (int)width,
                (int)height, (uint64_t)request_id);
            result(std::nullopt);
        }

        void SurfaceEndResize(
            int64_t handle,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            sparrow_handle_surface_end_resize((uint32_t)handle);
            result(std::nullopt);
        }

        void SurfaceToplevelSetSize(
            int64_t handle, int64_t width, int64_t height,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            sparrow_handle_surface_toplevel_set_size((uint32_t)handle, (int)width,
                (int)height);
            result(std::nullopt);
        }

        void SurfaceToplevelSetMaximized(
            int64_t handle, bool maximized,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            sparrow_handle_surface_toplevel_set_maximized((uint32_t)handle,
                maximized);
            result(std::nullopt);
        }

        void SurfaceToplevelClose(
            int64_t handle,
            std::function<void(sparrow::ErrorOr<bool> reply)> result) override
        {
            bool ok = sparrow_handle_surface_toplevel_close((uint32_t)handle);
            result(ok);
        }

        void SurfaceFocus(int64_t handle,
            std::function<void(std::optional<sparrow::FlutterError> reply)>
            result) override
        {
            sparrow_handle_surface_focus((uint32_t)handle);
            result(std::nullopt);
        }

        void SurfaceClearFocus(
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            Core *core = Core::instance();
            auto focused_surface = core->seat->keyboard_state.focused_surface;
            if (focused_surface)
            {
                const struct wlr_xdg_surface *current =
                    wlr_xdg_surface_try_from_wlr_surface(
                        core->seat->keyboard_state.focused_surface);
                if (current && current->initialized &&
                    (current->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) &&
                    current->toplevel)
                {
                    wlr_xdg_toplevel_set_activated(current->toplevel, false);
                    SparrowView *curr_view = static_cast<SparrowView*>(current->data);
                    if (curr_view != nullptr)
                    {
                        curr_view->activated = false;
                    }
                }

                sparrow_pointer_constraints_deactivate(core);
                wlr_seat_pointer_clear_focus(core->seat);
                wlr_seat_keyboard_clear_focus(core->seat);
                sparrow_cursor_reset_to_flutter();
            }

            result(std::nullopt);
        }

        void SurfaceSetPosition(
            int64_t handle, int64_t x, int64_t y,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            sparrow_handle_surface_set_position((uint32_t)handle, (int)x, (int)y);
            result(std::nullopt);
        }

        void ForceRenderAllViews(
            bool force,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            Core *core = Core::instance();
            wlr_log(WLR_INFO, "[OVERVIEW] force_render_all_views set to %s",
                force ? "TRUE" : "FALSE");
            core->force_render_all_views = force;
            if (core->force_render_all_views)
            {
                SparrowView *v = nullptr;
                wl_list_for_each(v, &core->views_list, link)
                {
                    if (v->texture_registered)
                    {
                        core->embedder_api.MarkExternalTextureFrameAvailable(core->engine,
                            v->texture_id);
                    }
                }
                sparrow_damage_add_box(nullptr);
            }

            result(std::nullopt);
        }

        void SetDirectInputMode(
            int64_t handle, bool enabled,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            Core *core = Core::instance();
            core->direct_input_mode    = enabled;
            core->direct_input_surface = (uint32_t)handle;
            wlr_log(WLR_INFO, "Direct input mode: %s for surface %lu",
                enabled ? "enabled" : "disabled", (unsigned long)handle);
            result(std::nullopt);
        }

        void SetPrimaryOutput(
            int64_t output_id,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            result(std::nullopt);
        }

        void SetVsyncOutput(
            int64_t output_id,
            std::function<void(sparrow::ErrorOr<bool> reply)> result) override
        {
            sparrow_set_vsync_output((uint32_t)output_id);
            result(true);
        }

        void SetVsyncRateLimit(
            int64_t max_hz,
            std::function<void(sparrow::ErrorOr<bool> reply)> result) override
        {
            sparrow_set_vsync_rate_limit((int)max_hz);
            result(true);
        }

        void SetOutputMode(
            int64_t output_id, int64_t width, int64_t height, int64_t refresh,
            std::function<void(sparrow::ErrorOr<bool> reply)> result) override
        {
            bool ok = sparrow_set_output_mode((uint32_t)output_id, (int)width,
                (int)height, (int)refresh);
            result(ok);
        }

        void SetOutputPosition(
            int64_t output_id, int64_t x, int64_t y,
            std::function<void(sparrow::ErrorOr<bool> reply)> result) override
        {
            bool ok =
                sparrow_set_output_position((uint32_t)output_id, (int)x, (int)y);
            result(ok);
        }

        void SetOutputScale(
            int64_t output_id, double scale,
            std::function<void(sparrow::ErrorOr<bool> reply)> result) override
        {
            bool ok = sparrow_set_output_scale((uint32_t)output_id, scale);
            result(ok);
        }

        void DebugSetDamageVisualization(
            bool enabled,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            Core *core = Core::instance();
            core->debug_damage = enabled;
            wlr_log(WLR_INFO, "Damage visualization: %s",
                enabled ? "ENABLED" : "DISABLED");
            sparrow_damage_add_box(nullptr);
            result(std::nullopt);
        }

        void DebugGetDamageVisualization(
            std::function<void(sparrow::ErrorOr<bool> reply)> result) override
        {
            Core *core = Core::instance();
            result(core->debug_damage);
        }

        void GetSocketPaths(std::function<
            void(sparrow::ErrorOr<sparrow::CompositorSocketsData> reply)>
            result) override
        {
            Core *core = Core::instance();
            sparrow::CompositorSocketsData data(
                core->wl_socket ? std::string(core->wl_socket) : std::string(""),
                std::string(""));
            result(data);
        }

        void CompositorReady(
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            Core *core = Core::instance();
            if (core)
            {
                wlr_log(
                    WLR_INFO,
                    "Dart compositor ready (via Pigeon), sending %d existing outputs",
                    wl_list_length(&core->outputs));
                sparrow_send_all_outputs();

                auto o = sparrow_get_first_output();
                if (o != nullptr)
                {
                    sparrow_touch *t = nullptr;
                    wl_list_for_each(t, &core->touchs, link)
                    {
                        map_touch_to_output(t->device, o);
                    }
                }
            }

            result(std::nullopt);
        }

        void IsCompositor(
            std::function<void(sparrow::ErrorOr<bool> reply)> result) override
        {
            result(true);
        }

        void SurfaceKeyboardKey(
            int64_t handle, int64_t keycode, int64_t status,
            int64_t timestamp_micros,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            struct surface_keyboard_key_message msg = {
                .surface_handle = (uint32_t)handle,
                .keycode    = (uint64_t)keycode,
                .event_type = (uint8_t)status,
                .timestamp  = timestamp_micros,
            };
            sparrow_handle_surface_keyboard_key(msg);
            result(std::nullopt);
        }

        void SurfacePointerEvent(
            const ::flutter::EncodableList & data,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            flutter::EncodableValue val(data);
            struct surface_pointer_event_message msg;
            if (decode_surface_pointer_event_message(&val, &msg))
            {
                sparrow_handle_surface_pointer_event(msg);
            }

            result(std::nullopt);
        }

        void PopupPointerEvent(
            const ::flutter::EncodableList & data,
            std::function<void(std::optional<sparrow::FlutterError> reply)> result)
        override
        {
            flutter::EncodableValue val(data);
            struct surface_pointer_event_message msg;
            if (decode_surface_pointer_event_message(&val, &msg))
            {
                sparrow_handle_popup_pointer_event(msg);
            }

            result(std::nullopt);
        }
    };

    instance->pigeon_host_api = std::make_unique<SparrowCompositorHostApi>();
    sparrow::CompositorHostApi::SetUp(&instance->messenger,
        instance->pigeon_host_api.get());
    instance->pigeon_flutter_api =
        std::make_unique<sparrow::CompositorFlutterApi>(&instance->messenger);
    wlr_log(WLR_INFO, "Pigeon tweet APIs initialized");
}

void sparrow_engine_reset_channels()
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return;
    }

    instance->pigeon_host_api.reset();
    instance->pigeon_flutter_api.reset();
    wlr_log(WLR_INFO, "Pigeon APIs reset");
}

void sparrow_send_lifecycle_state(const char *state_str)
{
    Core *instance = Core::instance();
    if ((instance == nullptr) || (instance->engine == nullptr))
    {
        return;
    }

    wlr_log(WLR_INFO, "[LIFECYCLE] Sent flutter/lifecycle state: %s", state_str);
    const uint8_t *data   = reinterpret_cast<const uint8_t*>(state_str);
    const size_t data_len = strlen(state_str);
    instance->messenger.Send("flutter/lifecycle", data, data_len, nullptr);
}

void sparrow_send_lifecycle_state_dpms(bool display_powered_on)
{
    static bool s_last_powered_on = true;
    if (s_last_powered_on == display_powered_on)
    {
        return;
    }

    s_last_powered_on = display_powered_on;

    if (!display_powered_on)
    {
        sparrow_send_lifecycle_state("AppLifecycleState.inactive");
        sparrow_send_lifecycle_state("AppLifecycleState.hidden");
        sparrow_send_lifecycle_state("AppLifecycleState.paused");
    } else
    {
        sparrow_send_lifecycle_state("AppLifecycleState.hidden");
        sparrow_send_lifecycle_state("AppLifecycleState.inactive");
        sparrow_send_lifecycle_state("AppLifecycleState.resumed");
    }

    Core *instance = Core::instance();
    if (instance && instance->ipc_server)
    {
        instance->ipc_server->broadcast_notification(
            "displayPower", display_powered_on ? "{\"powered_on\": true}" :
            "{\"powered_on\": false}");
    }
}

void engine_cb_platform_message(const FlutterPlatformMessage *engine_message,
    void *user_data)
{
    Core *instance = Core::instance();

    if (engine_message->struct_size != sizeof(FlutterPlatformMessage))
    {
        wlr_log(
            WLR_ERROR,
            "Invalid platform message size received. Expected %ld but received %ld",
            sizeof(FlutterPlatformMessage), engine_message->struct_size);
        return;
    }

    // Try the flutter:: client wrapper dispatch first.
    // Channels registered via MethodChannel::SetMethodCallHandler will be
    // handled here.
    if (instance->message_dispatcher &&
        instance->message_dispatcher->HandleMessage(*engine_message))
    {
        return;
    }

    wlr_log(WLR_DEBUG, "Unhandled platform message on channel: %s",
        engine_message->channel ? engine_message->channel : "<null>");
}

void engine_cb_log_message(const char *tag, const char *message,
    void *user_data)
{
    wlr_log(WLR_INFO, "DART [%s] %s", tag, message);
}
