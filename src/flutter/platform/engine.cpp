#include <EGL/egl.h>
#include <cassert>
#include <cstddef>
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
#include "input/pointer.hpp"
#include "output.hpp"
#include "surface/popup.hpp"
#include "surface/sub_surface.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"
#include "util/udmabuf.hpp"

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
// Two paths: DMA-BUF (preferred, zero-copy) or wlroots texture (fallback for
// SHM)
//
// Clean separation: Once buffer type is detected, we use only that path.
// This avoids overhead from repeatedly trying DMA-BUF for SHM surfaces.
// Helper to provide texture for Flutter external texture (platform views)
bool provide_surface_texture(struct wlr_surface *surface,
    size_t requested_width, size_t requested_height,
    FlutterOpenGLTexture *texture_out)
{
    if (!surface || !texture_out)
    {
        return false;
    }

#ifdef USE_DMABUF
    if (sparrow_renderer_import_surface_dmabuf(surface, texture_out))
    {
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

    return true;
}

bool engine_cb_external_texture(void *user_data, int64_t texture_id,
    size_t width, size_t height,
    FlutterOpenGLTexture *texture_out)
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return false;
    }

    pthread_mutex_lock(&instance->sparrow_renderer.texture_mutex);

    // First, try to find a view (toplevel surface)
    if (!wl_list_empty(&instance->views_list))
    {
        SparrowView *view = nullptr;
        SparrowView *v    = nullptr;
        wl_list_for_each(v, &instance->views_list, link)
        {
            if (v && (v->handle == (uint32_t)texture_id))
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
                    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
                    return true;
                }
            }

#endif
            if ((view->xdg_surface == nullptr) ||
                (view->xdg_surface->surface == nullptr))
            {
                wlr_log(WLR_DEBUG, "texture_id=%ld: view surface nullptr", texture_id);
                pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
                return false;
            }

            // Pass Flutter's requested dimensions for resize smoothing
            bool result = provide_surface_texture(view->xdg_surface->surface, width,
                height, texture_out);
            if (!result)
            {
                wlr_log(WLR_DEBUG,
                    "texture_id=%ld: provide_surface_texture returned false",
                    texture_id);
            }

            pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
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
                    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
                    return true;
                }
            }

#endif
            if (sub->surface == nullptr)
            {
                pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
                return false;
            }

            // Subsurfaces use actual size (no resize smoothing needed)
            bool result = provide_surface_texture(sub->surface, 0, 0, texture_out);
            pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
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
#ifdef USE_DMABUF
            if (popup->locked_buffer != nullptr)
            {
                if (sparrow_renderer_import_dmabuf_buffer(popup->locked_buffer,
                    texture_out))
                {
                    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
                    return true;
                }
            }

#endif
            if ((popup->xdg_surface == nullptr) ||
                (popup->xdg_surface->surface == nullptr))
            {
                pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
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

            if (sparrow_surface_get_texture(content_surface) == nullptr)
            {
                pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
                return false;
            }

            // Popups use actual size (no resize smoothing needed)
            const bool result =
                provide_surface_texture(content_surface, 0, 0, texture_out);

            // Keep requesting updates to catch subsurface content changes
            if (result && popup->texture_registered)
            {
                instance->embedder_api.MarkExternalTextureFrameAvailable(
                    instance->engine, popup->texture_id);
            }

            pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
            return result;
        }
    }

    pthread_mutex_unlock(&instance->sparrow_renderer.texture_mutex);
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

static inline int64_t enc_get_int(const flutter::EncodableValue & val,
    int64_t default_val = 0)
{
    if (std::holds_alternative<int32_t>(val))
    {
        return std::get<int32_t>(val);
    }

    if (std::holds_alternative<int64_t>(val))
    {
        return std::get<int64_t>(val);
    }

    if (std::holds_alternative<bool>(val))
    {
        return std::get<bool>(val) ? 1 : 0;
    }

    return default_val;
}

static inline double enc_get_double(const flutter::EncodableValue & val,
    double default_val = 0.0)
{
    if (std::holds_alternative<double>(val))
    {
        return std::get<double>(val);
    }

    if (std::holds_alternative<int32_t>(val))
    {
        return (double)std::get<int32_t>(val);
    }

    if (std::holds_alternative<int64_t>(val))
    {
        return (double)std::get<int64_t>(val);
    }

    return default_val;
}

static inline bool enc_get_bool(const flutter::EncodableValue & val,
    bool default_val = false)
{
    if (std::holds_alternative<bool>(val))
    {
        return std::get<bool>(val);
    }

    if (std::holds_alternative<int32_t>(val))
    {
        return std::get<int32_t>(val) != 0;
    }

    if (std::holds_alternative<int64_t>(val))
    {
        return std::get<int64_t>(val) != 0;
    }

    return default_val;
}

void sparrow_engine_init_channels()
{
    Core *instance = Core::instance();

    instance->wlroots_channel =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            &instance->messenger, "wlroots",
            &flutter::StandardMethodCodec::GetInstance());

    instance->wlroots_channel->SetMethodCallHandler(
        [] (const flutter::MethodCall<flutter::EncodableValue> & call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
            result)
    {
        Core *instance = Core::instance();
        const std::string & method = call.method_name();
        const auto *args = call.arguments();

        if (method == "surface_toplevel_set_size")
        {
            struct surface_toplevel_set_size_message msg;
            if (decode_surface_toplevel_set_size_message(args, &msg))
            {
                sparrow_handle_surface_toplevel_set_size(
                    msg.surface_handle, (int)msg.size_x, (int)msg.size_y);
                result->Success();
            } else
            {
                result->Error("invalid_args",
                    "Invalid surface_toplevel_set_size args");
            }
        } else if (method == "surface_toplevel_set_maximized")
        {
            struct surface_toplevel_set_maximized_message msg;
            if (decode_surface_toplevel_set_maximized_message(args, &msg))
            {
                sparrow_handle_surface_toplevel_set_maximized(msg.surface_handle,
                    msg.maximized != 0);
                result->Success();
            } else
            {
                result->Error("invalid_args",
                    "Invalid surface_toplevel_set_maximized args");
            }
        } else if (method == "surface_toplevel_close")
        {
            struct surface_toplevel_close_message msg;
            if (decode_surface_toplevel_close_message(args, &msg))
            {
                bool ok = sparrow_handle_surface_toplevel_close(msg.surface_handle);
                if (ok)
                {
                    result->Success();
                } else
                {
                    result->Error("error", "Failed to close toplevel");
                }
            } else
            {
                result->Error("invalid_args",
                    "Invalid surface_toplevel_close args");
            }
        } else if (method == "surface_focus")
        {
            struct surface_toplevel_close_message msg;
            if (decode_surface_toplevel_close_message(args, &msg))
            {
                sparrow_handle_surface_focus(msg.surface_handle);
                result->Success();
            } else
            {
                result->Error("invalid_args", "Invalid surface_focus args");
            }
        } else if (method == "surface_set_position")
        {
            struct surface_set_position_message msg;
            if (decode_surface_set_position_message(args, &msg))
            {
                sparrow_handle_surface_set_position(msg.surface_handle, (int)msg.x,
                    (int)msg.y);
                result->Success();
            } else
            {
                result->Error("invalid_args", "Invalid surface_set_position args");
            }
        } else if (method == "surface_request_resize")
        {
            const auto *list = std::get_if<flutter::EncodableList>(args);
            if (list && (list->size() >= 4))
            {
                uint32_t handle = (uint32_t)enc_get_int((*list)[0]);
                int width  = (int)enc_get_int((*list)[1]);
                int height = (int)enc_get_int((*list)[2]);
                uint64_t req_id = (uint64_t)enc_get_int((*list)[3]);
                sparrow_handle_surface_request_resize(handle, width, height,
                    req_id);
                result->Success();
            } else
            {
                result->Error("invalid_args",
                    "Invalid surface_request_resize args");
            }
        } else if (method == "surface_end_resize")
        {
            const auto *list = std::get_if<flutter::EncodableList>(args);
            if (list && (list->size() >= 1))
            {
                uint32_t handle = (uint32_t)enc_get_int((*list)[0]);
                sparrow_handle_surface_end_resize(handle);
                result->Success();
            } else
            {
                result->Error("invalid_args", "Invalid surface_end_resize args");
            }
        } else if (method == "surface_pointer_event")
        {
            struct surface_pointer_event_message msg;
            if (decode_surface_pointer_event_message(args, &msg))
            {
                sparrow_handle_surface_pointer_event(msg);
                result->Success();
            } else
            {
                result->Error("invalid_args", "Invalid surface_pointer_event args");
            }
        } else if (method == "popup_pointer_event")
        {
            struct surface_pointer_event_message msg;
            if (decode_surface_pointer_event_message(args, &msg))
            {
                sparrow_handle_popup_pointer_event(msg);
                result->Success();
            } else
            {
                result->Error("invalid_args", "Invalid popup_pointer_event args");
            }
        } else if (method == "surface_keyboard_key")
        {
            struct surface_keyboard_key_message msg;
            if (decode_surface_keyboard_key_message(args, &msg))
            {
                sparrow_handle_surface_keyboard_key(msg);
                result->Success();
            } else
            {
                result->Error("invalid_args", "Invalid surface_keyboard_key args");
            }
        } else if (method == "surface_clear_focus")
        {
            auto focused_surface = instance->seat->keyboard_state.focused_surface;
            if (focused_surface)
            {
                const struct wlr_xdg_surface *current =
                    wlr_xdg_surface_try_from_wlr_surface(
                        instance->seat->keyboard_state.focused_surface);
                if (current && (current->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL))
                {
                    wlr_xdg_toplevel_set_activated(current->toplevel, false);
                    SparrowView *curr_view =
                        static_cast<SparrowView*>(current->data);
                    if (curr_view != nullptr)
                    {
                        curr_view->activated = false;
                    }
                }

                sparrow_pointer_constraints_deactivate(instance);
                wlr_seat_pointer_clear_focus(instance->seat);
                wlr_seat_keyboard_clear_focus(instance->seat);
                sparrow_cursor_reset_to_flutter();
            }

            result->Success();
        } else if (method == "force_render_all_views")
        {
            const auto *list = std::get_if<flutter::EncodableList>(args);
            int active =
                (list && !list->empty()) ? (int)enc_get_int((*list)[0]) : 0;
            instance->force_render_all_views = (active != 0);
            if (instance->force_render_all_views)
            {
                SparrowView *v = nullptr;
                wl_list_for_each(v, &instance->views_list, link)
                {
                    if (v && v->texture_registered)
                    {
                        instance->embedder_api.MarkExternalTextureFrameAvailable(
                            instance->engine, v->texture_id);
                    }
                }
                sparrow_damage_add_box(nullptr);
            }

            result->Success();
        } else if (method == "set_direct_input_mode")
        {
            const auto *list = std::get_if<flutter::EncodableList>(args);
            if (list && (list->size() >= 2))
            {
                bool enabled = enc_get_bool((*list)[0]);
                uint32_t surface_handle     = (uint32_t)enc_get_int((*list)[1]);
                instance->direct_input_mode = enabled;
                instance->direct_input_surface = surface_handle;
                wlr_log(WLR_INFO, "Direct input mode: %s for surface %u",
                    enabled ? "enabled" : "disabled", surface_handle);
            }

            result->Success();
        } else if (method == "debug_set_damage_visualization")
        {
            if (args)
            {
                const auto *b = std::get_if<bool>(args);
                if (b)
                {
                    instance->debug_damage = *b;
                } else
                {
                    const auto *list = std::get_if<flutter::EncodableList>(args);
                    if (list && !list->empty())
                    {
                        instance->debug_damage = enc_get_bool((*list)[0]);
                    }
                }

                wlr_log(WLR_INFO, "Damage visualization: %s",
                    instance->debug_damage ? "ENABLED" : "DISABLED");
                sparrow_damage_add_box(nullptr);
                result->Success();
            } else
            {
                result->Error("invalid_args",
                    "Invalid debug_set_damage_visualization args");
            }
        } else if (method == "debug_get_damage_visualization")
        {
            flutter::EncodableValue res(instance->debug_damage);
            result->Success(res);
        } else if (method == "is_compositor")
        {
            result->Success();
        } else if (method == "compositor_ready")
        {
            wlr_log(WLR_INFO,
                "Dart compositor ready, sending %d existing outputs",
                wl_list_length(&instance->outputs));
            sparrow_send_all_outputs();

            auto o = sparrow_get_first_output();
            assert(o != nullptr);
            sparrow_touch *t = nullptr;
            wl_list_for_each(t, &instance->touchs, link)
            {
                map_touch_to_output(t->device, o);
            }
            result->Success();
        } else if (method == "get_socket_paths")
        {
            auto map = flutter::EncodableMap{
                {flutter::EncodableValue("wayland"),
                    flutter::EncodableValue(instance->wl_socket ?
                        std::string(instance->wl_socket) :
                        "")},
                {flutter::EncodableValue("x"), flutter::EncodableValue("")},
            };
            result->Success(flutter::EncodableValue(map));
        } else if (method == "set_vsync_output")
        {
            const auto *list = std::get_if<flutter::EncodableList>(args);
            if (list && !list->empty())
            {
                sparrow_set_vsync_output((uint32_t)enc_get_int((*list)[0]));
            }

            result->Success();
        } else if (method == "set_vsync_rate_limit")
        {
            const auto *list = std::get_if<flutter::EncodableList>(args);
            if (list && !list->empty())
            {
                sparrow_set_vsync_rate_limit((int)enc_get_int((*list)[0]));
            }

            result->Success();
        } else if (method == "set_output_mode")
        {
            const auto *list = std::get_if<flutter::EncodableList>(args);
            bool ok = false;
            if (list && (list->size() >= 4))
            {
                uint32_t output_id = (uint32_t)enc_get_int((*list)[0]);
                int width   = (int)enc_get_int((*list)[1]);
                int height  = (int)enc_get_int((*list)[2]);
                int refresh = (int)enc_get_int((*list)[3]);
                ok = sparrow_set_output_mode(output_id, width, height, refresh);
            }

            if (ok)
            {
                result->Success();
            } else
            {
                result->Error("error", "Failed to set output mode");
            }
        } else if (method == "set_output_position")
        {
            const auto *list = std::get_if<flutter::EncodableList>(args);
            bool ok = false;
            if (list && (list->size() >= 3))
            {
                uint32_t output_id = (uint32_t)enc_get_int((*list)[0]);
                int x = (int)enc_get_int((*list)[1]);
                int y = (int)enc_get_int((*list)[2]);
                ok    = sparrow_set_output_position(output_id, x, y);
            }

            if (ok)
            {
                result->Success();
            } else
            {
                result->Error("error", "Failed to set output position");
            }
        } else if (method == "set_output_scale")
        {
            const auto *list = std::get_if<flutter::EncodableList>(args);
            bool ok = false;
            if (list && (list->size() >= 2))
            {
                uint32_t output_id = (uint32_t)enc_get_int((*list)[0]);
                double scale = enc_get_double((*list)[1], 1.0);
                ok = sparrow_set_output_scale(output_id, scale);
            }

            if (ok)
            {
                result->Success();
            } else
            {
                result->Error("error", "Failed to set output scale");
            }
        } else if (method == "set_primary_output")
        {
            result->Success();
        } else
        {
            wlr_log(WLR_INFO, "Unhandled wlroots method: %s", method.c_str());
            result->NotImplemented();
        }
    });

    wlr_log(WLR_INFO,
        "wlroots channel initialized (modern flutter::MethodChannel)");
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
