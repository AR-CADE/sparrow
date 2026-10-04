#include "flutter_runner.hpp"
#include "input_manager.hpp"
#include "vulkan_window.hpp"
#include "wayland_window.hpp"

#include "client_wrapper/json_method_codec.h"
#include "client_wrapper/standard_method_codec.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <flutter/platform/runner/pigeon/messages.h>
#include <optional>
#include <simdjson.h>

class SparrowRunnerHostIpcApi : public sparrow::RunnerHostIpcApi
{
  public:
    void GetCompositorInfo(
        std::function<void(
            sparrow::ErrorOr<std::optional<sparrow::CompositorSystemInfoData>>
            reply)>
        result) override
    {
        printf(
            "[sparrow-app-runner] Pigeon IPC [sparrow/ipc]: getCompositorInfo\n");
        fflush(stdout);

        const auto client = FlutterRunner::instance()->get_ipc_client();
        if ((client == nullptr) || !client->is_connected())
        {
            result(sparrow::ErrorOr<std::optional<sparrow::CompositorSystemInfoData>>(
                std::nullopt));
            return;
        }

        client->send_request(
            "getCompositorInfo",
            [result] (bool success, const simdjson::dom::element & val)
        {
            printf("[sparrow-app-runner] getCompositorInfo IPC response: "
                   "success=%d, isObject=%d\n",
                success, success && val.is_object());
            fflush(stdout);

            if (!success || !val.is_object())
            {
                result(sparrow::ErrorOr<
                    std::optional<sparrow::CompositorSystemInfoData>>(
                        std::nullopt));
                return;
            }

            auto get_str = [] (const simdjson::dom::element & el, const char *key) -> std::string
            {
                auto res = el[key];
                if (!res.error() && res.is_string())
                {
                    auto s = res.get_string();
                    if (!s.error())
                    {
                        return std::string(s.value());
                    }
                }

                return "";
            };
            auto get_i64 = [] (const simdjson::dom::element & el, const char *key) -> int64_t
            {
                auto res = el[key];
                if (!res.error())
                {
                    int64_t v = 0;
                    if (res.get(v) == simdjson::SUCCESS)
                    {
                        return v;
                    }
                }

                return 0;
            };
            auto get_double = [] (const simdjson::dom::element & el, const char *key) -> double
            {
                auto res = el[key];
                if (!res.error())
                {
                    double v = 0.0;
                    if (res.get(v) == simdjson::SUCCESS)
                    {
                        return v;
                    }
                }

                return 0.0;
            };

            std::string compositor  = get_str(val, "compositor");
            std::string version     = get_str(val, "version");
            std::string ipc_channel = get_str(val, "ipc_channel");
            int64_t surfaces_count  = get_i64(val, "surfaces_count");
            double client_fps  = get_double(val, "client_fps");
            int64_t peer_pid   = get_i64(val, "peer_pid");
            std::string app_id = get_str(val, "app_id");

            result(std::make_optional(sparrow::CompositorSystemInfoData(
                compositor, version, ipc_channel, surfaces_count, client_fps,
                peer_pid, app_id)));
        });
    }

    void PingCompositor(
        std::function<void(
            sparrow::ErrorOr<std::optional<sparrow::CompositorPongData>> reply)>
        result) override
    {
        printf("[sparrow-app-runner] Pigeon IPC [sparrow/ipc]: ping\n");
        fflush(stdout);

        const auto client = FlutterRunner::instance()->get_ipc_client();
        if ((client == nullptr) || !client->is_connected())
        {
            printf("[sparrow-app-runner] pingCompositor: IPC client not connected\n");
            fflush(stdout);
            result(sparrow::ErrorOr<std::optional<sparrow::CompositorPongData>>(
                std::nullopt));
            return;
        }

        client->send_request(
            "ping", [result] (bool success, const simdjson::dom::element & val)
        {
            printf("[sparrow-app-runner] pingCompositor IPC response: "
                   "success=%d, isObject=%d\n",
                success, success && val.is_object());
            fflush(stdout);

            if (!success || !val.is_object())
            {
                result(sparrow::ErrorOr<std::optional<sparrow::CompositorPongData>>(
                    std::nullopt));
                return;
            }

            bool pong     = false;
            auto pong_res = val["pong"];
            if (!pong_res.error() && pong_res.is_bool())
            {
                pong = pong_res.get_bool().value();
            }

            int64_t timestamp_us = 0;
            auto ts_res = val["timestamp_us"];
            if (!ts_res.error())
            {
                (void)ts_res.get(timestamp_us);
            }

            int64_t peer_pid = 0;
            auto pid_res     = val["peer_pid"];
            if (!pid_res.error())
            {
                (void)pid_res.get(peer_pid);
            }

            result(std::make_optional(
                sparrow::CompositorPongData(pong, timestamp_us, peer_pid)));
        });
    }

    void ListSurfaces(
        std::function<
            void(sparrow::ErrorOr<std::optional<::flutter::EncodableList>> reply)>
        result) override
    {
        printf("[sparrow-app-runner] Pigeon IPC [sparrow/ipc]: listSurfaces\n");
        fflush(stdout);

        const auto client = FlutterRunner::instance()->get_ipc_client();
        if ((client == nullptr) || !client->is_connected())
        {
            result(sparrow::ErrorOr<std::optional<::flutter::EncodableList>>(
                std::nullopt));
            return;
        }

        client->send_request(
            "listSurfaces",
            [result] (bool success, const simdjson::dom::element & val)
        {
            if (!success || !val.is_array())
            {
                result(sparrow::ErrorOr<std::optional<::flutter::EncodableList>>(
                    std::nullopt));
                return;
            }

            auto get_str = [] (const simdjson::dom::element & el, const char *key) -> std::string
            {
                auto res = el[key];
                if (!res.error() && res.is_string())
                {
                    auto s = res.get_string();
                    if (!s.error())
                    {
                        return std::string(s.value());
                    }
                }

                return "";
            };
            auto get_i64 = [] (const simdjson::dom::element & el, const char *key) -> int64_t
            {
                auto res = el[key];
                if (!res.error())
                {
                    int64_t v = 0;
                    if (res.get(v) == simdjson::SUCCESS)
                    {
                        return v;
                    }
                }

                return 0;
            };
            auto get_bool = [] (const simdjson::dom::element & el, const char *key) -> bool
            {
                auto res = el[key];
                if (!res.error() && res.is_bool())
                {
                    bool v = false;
                    if (res.get(v) == simdjson::SUCCESS)
                    {
                        return v;
                    }
                }

                return false;
            };

            ::flutter::EncodableList surfaces_list;
            for (const auto & item : val.get_array().value())
            {
                if (item.is_object())
                {
                    std::string title  = get_str(item, "title");
                    int64_t handle     = get_i64(item, "handle");
                    std::string app_id = get_str(item, "app_id");
                    int64_t width   = get_i64(item, "width");
                    int64_t height  = get_i64(item, "height");
                    bool fullscreen = get_bool(item, "fullscreen");
                    bool maximized  = get_bool(item, "maximized");
                    bool activated  = get_bool(item, "activated");

                    sparrow::CompositorSurfaceData surface_data(
                        title, handle, app_id, width, height, fullscreen, maximized,
                        activated);
                    surfaces_list.push_back(
                        ::flutter::CustomEncodableValue(surface_data));
                }
            }

            result(std::make_optional(std::move(surfaces_list)));
        });
    }

    void CloseSurface(
        int64_t handle,
        std::function<void(sparrow::ErrorOr<bool> reply)> result) override
    {
        printf("[sparrow-app-runner] Pigeon IPC [sparrow/ipc]: closeSurface\n");
        fflush(stdout);

        const auto client = FlutterRunner::instance()->get_ipc_client();
        if ((client == nullptr) || !client->is_connected())
        {
            result(false);
            return;
        }

        std::string params = "{\"handle\":" + std::to_string(handle) + "}";

        client->send_request("closeSurface", params,
            [result] (bool success, const simdjson::dom::element & val)
        {
            if (success && val.is_object())
            {
                auto closed_res = val["closed"];
                if (!closed_res.error() && closed_res.is_bool())
                {
                    result(closed_res.get_bool().value());
                    return;
                }
            }

            result(success);
        });
    }
};

class SparrowRunnerHostApi : public sparrow::RunnerHostApi
{
  public:
    void GetSystemInfo(
        std::function<void(
            sparrow::ErrorOr<std::optional<sparrow::RunnerSystemInfoData>> reply)>
        result) override
    {
        auto window = FlutterRunner::instance()->get_window();
        if (window == nullptr)
        {
            result(sparrow::ErrorOr<std::optional<sparrow::RunnerSystemInfoData>>(
                std::nullopt));
            return;
        }

        const char *wayland_display = getenv("WAYLAND_DISPLAY");
        sparrow::RunnerSystemInfoData system_info_data(
            "sparrow-app-runner", "0.2.0",
            wayland_display != nullptr ? std::string(wayland_display) : "",
            window->get_width(), window->get_height(), window->get_pixel_ratio(),
            window->is_fullscreen(), window->is_maximized(), window->get_title(),
            window->get_app_id());
        result(std::make_optional(system_info_data));
    }

    void SetWindowTitle(
        const std::string & title,
        std::function<void(sparrow::ErrorOr<bool> reply)> result) override
    {
        auto window = FlutterRunner::instance()->get_window();
        if (!window)
        {
            result(false);
            return;
        }

        window->set_title(title);
        result(true);
    }

    void SetFullscreen(
        bool enabled,
        std::function<void(sparrow::ErrorOr<bool> reply)> result) override
    {
        auto window = FlutterRunner::instance()->get_window();
        if (!window)
        {
            result(false);
            return;
        }

        window->set_fullscreen(enabled);
        result(true);
    }

    void SetMaximized(
        bool enabled,
        std::function<void(sparrow::ErrorOr<bool> reply)> result) override
    {
        auto window = FlutterRunner::instance()->get_window();
        if (!window)
        {
            result(false);
            return;
        }

        window->set_maximized(enabled);
        result(true);
    }

    void Minimize(std::function<void(sparrow::ErrorOr<bool> reply)> result) override
    {
        auto window = FlutterRunner::instance()->get_window();
        if (!window)
        {
            result(false);
            return;
        }

        window->minimize();
        result(true);
    }

    void Ping(std::function<void(sparrow::ErrorOr<sparrow::RunnerPongData> reply)>
        result) override
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        int64_t ms = static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
        sparrow::RunnerPongData pong_data(true, std::to_string(ms));
        result(pong_data);
    }
};

std::atomic<FlutterRunner*> FlutterRunner::_instance{nullptr};
std::mutex FlutterRunner::m_;

FlutterRunner*FlutterRunner::instance()
{
    FlutterRunner *instance = _instance.load(std::memory_order_relaxed);
    if (instance == nullptr)
    {
        std::lock_guard<std::mutex> lock(m_);
        if (_instance == nullptr)
        {
            return nullptr;
        }
    }

    return instance;
}

FlutterRunner::FlutterRunner(WaylandWindow *window, InputManager *input_manager,
    RendererBackend backend, bool enable_vk_validation,
    VkPresentModeKHR preferred_present_mode) :
    backend_(backend), enable_vk_validation_(enable_vk_validation),
    preferred_present_mode_(preferred_present_mode), window_(window),
    input_manager_(input_manager)
{
    _instance.store(this, std::memory_order_release);
}

FlutterRunner::~FlutterRunner()
{
    shutdown();
}

void FlutterRunner::on_platform_message(const FlutterPlatformMessage *message)
{
    if (!message || (message->struct_size != sizeof(FlutterPlatformMessage)))
    {
        return;
    }

    printf("[sparrow-app-runner] Incoming PlatformMessage on channel: '%s'\n",
        message->channel);
    fflush(stdout);

    if (message_dispatcher_ && message_dispatcher_->HandleMessage(*message))
    {
        return;
    }

    // Acknowledge unhandled message with empty response to unblock Dart Futures
    if (message->response_handle)
    {
        embedder_api_.SendPlatformMessageResponse(engine_, message->response_handle,
            nullptr, 0);
    }
}

bool FlutterRunner::init(const std::string & assets_path,
    const std::string & icu_data_path,
    const std::string & aot_elf_path,
    const std::vector<std::string> & engine_args)
{
    embedder_api_.struct_size = sizeof(FlutterEngineProcTable);
    if (FlutterEngineGetProcAddresses(&embedder_api_) != kSuccess)
    {
        fprintf(
            stderr,
            "[sparrow-app-runner] Failed to get Flutter engine proc addresses\n");
        return false;
    }

    FlutterRendererConfig renderer_config = {};
    if (backend_ == RendererBackend::kVulkan)
    {
        vulkan_window_ = std::make_unique<VulkanWindow>();
        if (!vulkan_window_->init(window_->get_display(), window_->get_surface(),
            window_->get_width(), window_->get_height(),
            enable_vk_validation_, preferred_present_mode_))
        {
            fprintf(stderr,
                "[sparrow-app-runner] Failed to initialize Vulkan window\n");
            return false;
        }

        window_->on_window_resized = [this] (int32_t width, int32_t height)
        {
            if (vulkan_window_)
            {
                vulkan_window_->mark_resize(width, height);
            }
        };

        renderer_config.type = kVulkan;
        renderer_config.vulkan.struct_size = sizeof(FlutterVulkanRendererConfig);
        renderer_config.vulkan.version     = VK_API_VERSION_1_2;
        renderer_config.vulkan.instance    = vulkan_window_->get_instance();
        renderer_config.vulkan.physical_device =
            vulkan_window_->get_physical_device();
        renderer_config.vulkan.device = vulkan_window_->get_device();
        renderer_config.vulkan.queue_family_index =
            vulkan_window_->get_queue_family_index();
        renderer_config.vulkan.queue = vulkan_window_->get_queue();
        renderer_config.vulkan.enabled_instance_extension_count =
            vulkan_window_->get_enabled_instance_extensions().size();
        renderer_config.vulkan.enabled_instance_extensions =
            const_cast<const char**>(
                vulkan_window_->get_enabled_instance_extensions().data());
        renderer_config.vulkan.enabled_device_extension_count =
            vulkan_window_->get_enabled_device_extensions().size();
        renderer_config.vulkan.enabled_device_extensions =
            const_cast<const char**>(
                vulkan_window_->get_enabled_device_extensions().data());
        renderer_config.vulkan.get_instance_proc_address_callback =
            VulkanWindow::get_instance_proc_address;
        renderer_config.vulkan.get_next_image_callback =
            [] (void *user_data,
                const FlutterFrameInfo *frame_info) -> FlutterVulkanImage
        {
            auto *self = static_cast<FlutterRunner*>(user_data);
            return self->vulkan_window_->get_next_image(frame_info);
        };
        renderer_config.vulkan.present_image_callback =
            [] (void *user_data, const FlutterVulkanImage *image) -> bool
        {
            auto *self = static_cast<FlutterRunner*>(user_data);
            return self->vulkan_window_->present_image(image);
        };
    } else
    {
        renderer_config.type = kOpenGL;
        renderer_config.open_gl.struct_size  = sizeof(FlutterOpenGLRendererConfig);
        renderer_config.open_gl.make_current = [] (void *user_data) -> bool
        {
            auto *self = static_cast<FlutterRunner*>(user_data);
            return self->window_->make_current();
        };
        renderer_config.open_gl.clear_current = [] (void *user_data) -> bool
        {
            auto *self = static_cast<FlutterRunner*>(user_data);
            return self->window_->clear_current();
        };
        renderer_config.open_gl.present = [] (void *user_data) -> bool
        {
            auto *self = static_cast<FlutterRunner*>(user_data);
            return self->window_->swap_buffers();
        };
        renderer_config.open_gl.fbo_callback = [] (void *user_data) -> uint32_t
        {
            (void)user_data;
            return 0; // Default window framebuffer
        };
        renderer_config.open_gl.make_resource_current =
            [] (void *user_data) -> bool
        {
            auto *self = static_cast<FlutterRunner*>(user_data);
            return self->window_->make_resource_current();
        };
        renderer_config.open_gl.gl_proc_resolver = WaylandWindow::gl_proc_resolver;
    }

    std::vector<const char*> argv_ptrs;
    argv_ptrs.push_back("sparrow-app-runner");
    bool has_impeller_flag = false;
    for (const auto & arg : engine_args)
    {
        if (arg.rfind("--enable-impeller", 0) == 0)
        {
            has_impeller_flag = true;
        }

        argv_ptrs.push_back(arg.c_str());
    }

    if ((backend_ == RendererBackend::kVulkan) && !has_impeller_flag)
    {
        argv_ptrs.push_back("--enable-impeller=true");
    }

    FlutterProjectArgs project_args = {};
    project_args.struct_size   = sizeof(FlutterProjectArgs);
    project_args.assets_path   = assets_path.c_str();
    project_args.icu_data_path = icu_data_path.c_str();
    project_args.command_line_argc = static_cast<int>(argv_ptrs.size());
    project_args.command_line_argv = argv_ptrs.data();
    project_args.platform_message_callback = [] (const FlutterPlatformMessage *msg,
                                                 void *user_data)
    {
        auto *self = static_cast<FlutterRunner*>(user_data);
        self->on_platform_message(msg);
    };

    project_args.log_message_callback = [] (const char *tag, const char *message,
                                            void *user_data)
    {
        (void)user_data;
        printf("[DART] [%s] %s\n", tag ? tag : "flutter", message ? message : "");
        fflush(stdout);
    };

    main_thread_id_ = std::this_thread::get_id();
    platform_task_runner_.struct_size = sizeof(FlutterTaskRunnerDescription);
    platform_task_runner_.user_data   = this;
    platform_task_runner_.runs_task_on_current_thread_callback =
        [] (void *user_data) -> bool
    {
        auto *self = static_cast<FlutterRunner*>(user_data);
        return std::this_thread::get_id() == self->main_thread_id_;
    };
    platform_task_runner_.post_task_callback =
        [] (FlutterTask task, uint64_t target_time, void *user_data)
    {
        auto *self = static_cast<FlutterRunner*>(user_data);
        std::scoped_lock lock(self->task_mutex_);
        self->queued_tasks_.push_back({task, target_time});
    };

    custom_task_runners_.struct_size = sizeof(FlutterCustomTaskRunners);
    custom_task_runners_.platform_task_runner = &platform_task_runner_;
    project_args.custom_task_runners = &custom_task_runners_;

    if (!aot_elf_path.empty() && embedder_api_.RunsAOTCompiledDartCode &&
        embedder_api_.RunsAOTCompiledDartCode())
    {
        FlutterEngineAOTDataSource aot_source = {};
        aot_source.type     = kFlutterEngineAOTDataSourceTypeElfPath;
        aot_source.elf_path = aot_elf_path.c_str();

        if (FlutterEngineCreateAOTData(&aot_source, &aot_data_) != kSuccess)
        {
            fprintf(stderr,
                "[sparrow-app-runner] FlutterEngineCreateAOTData failed for %s\n",
                aot_elf_path.c_str());
            return false;
        }

        project_args.aot_data = aot_data_;
    }

    // Prepare client wrapper dispatcher
    message_dispatcher_ =
        std::make_unique<IncomingMessageDispatcher>(&messenger_);
    messenger_.SetMessageDispatcher(message_dispatcher_.get());

    const FlutterEngineResult run_result = embedder_api_.Run(
        FLUTTER_ENGINE_VERSION, &renderer_config, &project_args, this, &engine_);

    if ((run_result != kSuccess) || !engine_)
    {
        fprintf(stderr,
            "[sparrow-app-runner] FlutterEngineRun failed with code: %d\n",
            run_result);
        return false;
    }

    messenger_.SetEngine(engine_, &embedder_api_);
    input_manager_->set_engine(engine_);

    if (window_)
    {
        window_->set_ipc_client(&ipc_client_);
        window_->on_window_activated = [this] (bool activated)
        {
            set_window_active(activated);
        };
        window_->on_ipc_fd_received = [this] (int fd)
        {
            ipc_client_.set_fd(fd);
            if (runner_flutter_ipc_api_)
            {
                runner_flutter_ipc_api_->OnNotification(
                    "ipcConnected", nullptr, [] () {},
                    [] (const sparrow::FlutterError &) {});
            }
        };
    }

    init_platform_channels();

    // Send initial window metrics
    send_window_metrics(window_->get_width(), window_->get_height(),
        window_->get_pixel_ratio());

    return true;
}

void FlutterRunner::init_platform_channels()
{
    // 1. flutter/platform channel for clipboard & system navigator
    platform_channel_ =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            &messenger_, "flutter/platform",
            &flutter::JsonMethodCodec::GetInstance());

    platform_channel_->SetMethodCallHandler(
        [this] (
            const flutter::MethodCall<flutter::EncodableValue> & call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
    {
        const std::string & method = call.method_name();
        printf("[sparrow-app-runner] MethodChannel [flutter/platform]: %s\n",
            method.c_str());
        fflush(stdout);

        if (method == "Clipboard.setData")
        {
            const auto *args = call.arguments();
            if (args && std::holds_alternative<flutter::EncodableMap>(*args))
            {
                const auto & map = std::get<flutter::EncodableMap>(*args);
                auto text_it     = map.find(flutter::EncodableValue("text"));
                if ((text_it != map.end()) && std::holds_alternative<std::string>(text_it->second))
                {
                    std::string text = std::get<std::string>(text_it->second);
                    clipboard_text_  = text;
                    if (window_)
                    {
                        window_->set_clipboard_text(text);
                    }
                }
            }

            result->Success();
        } else if (method == "Clipboard.getData")
        {
            std::string text = clipboard_text_;
            if (window_)
            {
                text = window_->get_clipboard_text();
                clipboard_text_ = text;
            }

            flutter::EncodableMap response;
            response[flutter::EncodableValue("text")] = flutter::EncodableValue(text);
            result->Success(flutter::EncodableValue(std::move(response)));
        } else if (method == "Clipboard.hasStrings")
        {
            bool has_str = !clipboard_text_.empty();
            if (window_)
            {
                has_str = window_->has_clipboard_text();
            }

            flutter::EncodableMap response;
            response[flutter::EncodableValue("value")] = flutter::EncodableValue(has_str);
            result->Success(flutter::EncodableValue(std::move(response)));
        } else if (method == "SystemNavigator.pop")
        {
            window_->request_close();
            result->Success();
        } else if ((method == "SystemSound.play") ||
                   (method == "System.initializationComplete") ||
                   (method ==
                    "SystemChrome.setApplicationSwitcherDescription") ||
                   (method == "SystemChrome.setSystemUIOverlayStyle") ||
                   (method == "LiveText.isLiveTextInputAvailable"))
        {
            result->Success();
        } else
        {
            result->NotImplemented();
        }
    });

    // 2. flutter/textinput channel for text input editing
    text_input_channel_ =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            &messenger_, "flutter/textinput",
            &flutter::JsonMethodCodec::GetInstance());

    text_input_channel_->SetMethodCallHandler(
        [this] (
            const flutter::MethodCall<flutter::EncodableValue> & call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
    {
        const std::string & method = call.method_name();

        if (method == "TextInput.setClient")
        {
            const auto *args = call.arguments();
            if (args && std::holds_alternative<flutter::EncodableList>(*args))
            {
                const auto & arr = std::get<flutter::EncodableList>(*args);
                if (arr.size() >= 2)
                {
                    text_input_state_.active = true;
                    if (std::holds_alternative<int32_t>(arr[0]) || std::holds_alternative<int64_t>(arr[0]))
                    {
                        text_input_state_.client_id = arr[0].LongValue();
                    }

                    text_input_state_.text.clear();
                    text_input_state_.selection_base   = 0;
                    text_input_state_.selection_extent = 0;
                    text_input_state_.composing_base   = -1;
                    text_input_state_.composing_extent = -1;
                    text_input_state_.multiline    = false;
                    text_input_state_.input_action = "TextInputAction.done";

                    if (std::holds_alternative<flutter::EncodableMap>(arr[1]))
                    {
                        const auto & config = std::get<flutter::EncodableMap>(arr[1]);
                        auto action_it = config.find(flutter::EncodableValue("inputAction"));
                        if ((action_it != config.end()) &&
                            std::holds_alternative<std::string>(action_it->second))
                        {
                            text_input_state_.input_action = std::get<std::string>(action_it->second);
                        }

                        auto type_it = config.find(flutter::EncodableValue("inputType"));
                        if ((type_it != config.end()) &&
                            std::holds_alternative<flutter::EncodableMap>(type_it->second))
                        {
                            const auto & input_type = std::get<flutter::EncodableMap>(type_it->second);
                            auto name_it = input_type.find(flutter::EncodableValue("name"));
                            if ((name_it != input_type.end()) &&
                                std::holds_alternative<std::string>(name_it->second))
                            {
                                if (std::get<std::string>(name_it->second).find("multiline") !=
                                    std::string::npos)
                                {
                                    text_input_state_.multiline = true;
                                }
                            }
                        }
                    }
                }
            }

            result->Success();
        } else if (method == "TextInput.setEditingState")
        {
            const auto *args = call.arguments();
            if (args && std::holds_alternative<flutter::EncodableMap>(*args))
            {
                const auto & state = std::get<flutter::EncodableMap>(*args);
                auto text_it = state.find(flutter::EncodableValue("text"));
                if ((text_it != state.end()) && std::holds_alternative<std::string>(text_it->second))
                {
                    text_input_state_.text = std::get<std::string>(text_it->second);
                }

                auto base_it = state.find(flutter::EncodableValue("selectionBase"));
                if ((base_it != state.end()) &&
                    (std::holds_alternative<int32_t>(base_it->second) ||
                     std::holds_alternative<int64_t>(base_it->second)))
                {
                    text_input_state_.selection_base = static_cast<int32_t>(base_it->second.LongValue());
                }

                auto ext_it = state.find(flutter::EncodableValue("selectionExtent"));
                if ((ext_it != state.end()) &&
                    (std::holds_alternative<int32_t>(ext_it->second) ||
                     std::holds_alternative<int64_t>(ext_it->second)))
                {
                    text_input_state_.selection_extent = static_cast<int32_t>(ext_it->second.LongValue());
                }

                int32_t text_len =
                    static_cast<int32_t>(text_input_state_.text.length());
                if ((text_input_state_.selection_base < 0) ||
                    (text_input_state_.selection_base > text_len))
                {
                    text_input_state_.selection_base = text_len;
                }

                if ((text_input_state_.selection_extent < 0) ||
                    (text_input_state_.selection_extent > text_len))
                {
                    text_input_state_.selection_extent =
                        text_input_state_.selection_base;
                }

                auto comp_b_it = state.find(flutter::EncodableValue("composingBase"));
                if ((comp_b_it != state.end()) &&
                    (std::holds_alternative<int32_t>(comp_b_it->second) ||
                     std::holds_alternative<int64_t>(comp_b_it->second)))
                {
                    text_input_state_.composing_base = static_cast<int32_t>(comp_b_it->second.LongValue());
                }

                auto comp_e_it = state.find(flutter::EncodableValue("composingExtent"));
                if ((comp_e_it != state.end()) &&
                    (std::holds_alternative<int32_t>(comp_e_it->second) ||
                     std::holds_alternative<int64_t>(comp_e_it->second)))
                {
                    text_input_state_.composing_extent = static_cast<int32_t>(comp_e_it->second.LongValue());
                }
            }

            result->Success();
        } else if (method == "TextInput.clearClient")
        {
            text_input_state_.active    = false;
            text_input_state_.client_id = 0;
            text_input_state_.text.clear();
            result->Success();
        } else if ((method == "TextInput.show") ||
                   (method == "TextInput.hide") ||
                   (method == "TextInput.setEditableSizeAndTransform") ||
                   (method == "TextInput.setMarkedTextRect") ||
                   (method == "TextInput.setStyle") ||
                   (method == "TextInput.setCaretRect") ||
                   (method == "TextInput.requestAutofill") ||
                   (method == "TextInput.finishAutofillContext"))
        {
            result->Success();
        } else
        {
            result->NotImplemented();
        }
    });

    // Hook input manager keyboard typing to text input model
    input_manager_->on_key_event = [this] (xkb_keysym_t keysym, uint32_t unicode,
                                           bool pressed)
    {
        handle_text_input_key(keysym, unicode, pressed);
    };

    // 3. sparrow system pigeon APIs
    runner_host_api_ = std::make_unique<SparrowRunnerHostApi>();
    sparrow::RunnerHostApi::SetUp(&messenger_, runner_host_api_.get());
    // pigeon_flutter_api =
    // std::make_unique<sparrow::RunnerFlutterApi>(&messenger_);

    // 4. flutter/mousecursor channel
    mouse_cursor_channel_ =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            &messenger_, "flutter/mousecursor",
            &flutter::StandardMethodCodec::GetInstance());

    mouse_cursor_channel_->SetMethodCallHandler(
        [this] (const flutter::MethodCall<flutter::EncodableValue> & call,
                std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
                result)
    {
        const std::string & method = call.method_name();
        if (method == "activateSystemCursor")
        {
            const auto *args =
                std::get_if<flutter::EncodableMap>(call.arguments());
            if (args)
            {
                auto kind_it = args->find(flutter::EncodableValue("kind"));
                if ((kind_it != args->end()) &&
                    std::holds_alternative<std::string>(kind_it->second))
                {
                    const std::string & flutter_kind =
                        std::get<std::string>(kind_it->second);
                    if (input_manager_)
                    {
                        input_manager_->set_cursor(flutter_kind);
                    }
                }
            }

            result->Success();
        } else if ((method == "createSystemCursor") ||
                   (method == "deleteSystemCursor"))
        {
            result->Success();
        } else
        {
            result->NotImplemented();
        }
    });

    // 5. sparrow compositor IPC pigeon APIs
    runner_host_ipc_api_ = std::make_unique<SparrowRunnerHostIpcApi>();
    sparrow::RunnerHostIpcApi::SetUp(&messenger_, runner_host_ipc_api_.get());

    runner_flutter_ipc_api_ =
        std::make_unique<sparrow::RunnerFlutterIpcApi>(&messenger_);

    ipc_client_.set_notification_handler(
        [this] (const std::string & method, const simdjson::dom::element & params)
    {
        printf("[sparrow-app-runner] Compositor notification: %s\n",
            method.c_str());
        fflush(stdout);

        if (method == "displayPower")
        {
            if (params.is_object())
            {
                auto p_res = params["powered_on"];
                if (!p_res.error() && p_res.is_bool())
                {
                    bool powered = p_res.get_bool().value();
                    set_display_powered(powered);
                }
            }
        }

        std::string params_json;
        const std::string *params_json_ptr = nullptr;
        if (!params.is_null())
        {
            params_json     = simdjson::to_string(params);
            params_json_ptr = &params_json;
        }

        if (runner_flutter_ipc_api_)
        {
            runner_flutter_ipc_api_->OnNotification(
                method, params_json_ptr, [] () {},
                [] (const sparrow::FlutterError & error)
            {
                fprintf(stderr,
                    "[sparrow-app-runner] Error forwarding notification to "
                    "Flutter: %s\n",
                    error.message().c_str());
            });
        }
    });
}

void FlutterRunner::send_lifecycle_state(const char *state_str)
{
    if (!engine_)
    {
        return;
    }

    printf("[sparrow-app-runner] [LIFECYCLE] Sent flutter/lifecycle state: %s\n",
        state_str);
    fflush(stdout);
    const uint8_t *data   = reinterpret_cast<const uint8_t*>(state_str);
    const size_t data_len = strlen(state_str);
    messenger_.Send("flutter/lifecycle", data, data_len, nullptr);
}

void FlutterRunner::set_window_active(bool active)
{
    if (window_active_ == active)
    {
        return;
    }

    window_active_ = active;
    printf("[sparrow-app-runner] Window activation changed: %s\n",
        active ? "ACTIVE" : "INACTIVE");
    fflush(stdout);
    update_lifecycle_state();
}

void FlutterRunner::set_display_powered(bool powered)
{
    if (display_powered_ == powered)
    {
        return;
    }

    display_powered_ = powered;
    printf("[sparrow-app-runner] Display power changed: %s\n",
        powered ? "ON" : "OFF");
    fflush(stdout);
    update_lifecycle_state();
}

void FlutterRunner::update_lifecycle_state()
{
    std::string target_state;
    if (!display_powered_)
    {
        target_state = "AppLifecycleState.paused";
    } else if (window_active_)
    {
        target_state = "AppLifecycleState.resumed";
    } else
    {
        target_state = "AppLifecycleState.inactive";
    }

    if (current_lifecycle_state_ == target_state)
    {
        return;
    }

    if (target_state == "AppLifecycleState.paused")
    {
        if (current_lifecycle_state_ == "AppLifecycleState.resumed")
        {
            send_lifecycle_state("AppLifecycleState.inactive");
            send_lifecycle_state("AppLifecycleState.hidden");
        } else if (current_lifecycle_state_ == "AppLifecycleState.inactive")
        {
            send_lifecycle_state("AppLifecycleState.hidden");
        }

        send_lifecycle_state("AppLifecycleState.paused");
    } else if (target_state == "AppLifecycleState.resumed")
    {
        if (current_lifecycle_state_ == "AppLifecycleState.paused")
        {
            send_lifecycle_state("AppLifecycleState.hidden");
            send_lifecycle_state("AppLifecycleState.inactive");
        } else if (current_lifecycle_state_ == "AppLifecycleState.hidden")
        {
            send_lifecycle_state("AppLifecycleState.inactive");
        }

        send_lifecycle_state("AppLifecycleState.resumed");
    } else if (target_state == "AppLifecycleState.inactive")
    {
        if (current_lifecycle_state_ == "AppLifecycleState.paused")
        {
            send_lifecycle_state("AppLifecycleState.hidden");
            send_lifecycle_state("AppLifecycleState.inactive");
        } else if (current_lifecycle_state_ == "AppLifecycleState.hidden")
        {
            send_lifecycle_state("AppLifecycleState.inactive");
        } else if (current_lifecycle_state_ == "AppLifecycleState.resumed")
        {
            send_lifecycle_state("AppLifecycleState.inactive");
        }
    }

    current_lifecycle_state_ = target_state;
}

void FlutterRunner::send_window_metrics(int32_t width, int32_t height,
    double pixel_ratio)
{
    if (!engine_ || (width <= 0) || (height <= 0))
    {
        return;
    }

    const double pr = (pixel_ratio > 0.0) ? pixel_ratio : 1.0;

    FlutterWindowMetricsEvent metrics_event = {};
    metrics_event.struct_size = sizeof(FlutterWindowMetricsEvent);
    metrics_event.width  = static_cast<size_t>(width * pr);
    metrics_event.height = static_cast<size_t>(height * pr);
    metrics_event.pixel_ratio = pr;

    FlutterEngineSendWindowMetricsEvent(engine_, &metrics_event);
}

void FlutterRunner::process_tasks()
{
    if (!engine_ || !embedder_api_.RunTask)
    {
        return;
    }

    const uint64_t current_time_nanos =
        embedder_api_.GetCurrentTime ? embedder_api_.GetCurrentTime() : 0;

    std::vector<FlutterTask> ready_tasks;
    {
        std::scoped_lock lock(task_mutex_);
        if (queued_tasks_.empty())
        {
            return;
        }

        std::vector<QueuedTask> remaining;
        remaining.reserve(queued_tasks_.size());
        ready_tasks.reserve(queued_tasks_.size());

        for (const auto & item : queued_tasks_)
        {
            if (current_time_nanos >= item.target_time_nanos)
            {
                ready_tasks.push_back(item.task);
            } else
            {
                remaining.push_back(item);
            }
        }

        queued_tasks_ = std::move(remaining);
    }

    for (const auto & task : ready_tasks)
    {
        embedder_api_.RunTask(engine_, &task);
    }
}

void FlutterRunner::shutdown()
{
    if (engine_)
    {
        send_lifecycle_state("AppLifecycleState.detached");
        printf("[sparrow-app-runner] Calling FlutterEngineShutdown...\n");
        fflush(stdout);
        messenger_.SetEngine(nullptr, nullptr);
        embedder_api_.Shutdown(engine_);
        engine_ = nullptr;
        printf("[sparrow-app-runner] FlutterEngineShutdown returned.\n");
        fflush(stdout);
    }

    mouse_cursor_channel_.reset();
    runner_host_api_.reset();
    text_input_channel_.reset();
    platform_channel_.reset();

    if (aot_data_)
    {
        embedder_api_.CollectAOTData(aot_data_);
        aot_data_ = nullptr;
    }

    if (vulkan_window_)
    {
        vulkan_window_->shutdown();
        vulkan_window_.reset();
    }
}

void FlutterRunner::handle_text_input_key(xkb_keysym_t keysym, uint32_t unicode,
    bool pressed)
{
    if (!text_input_state_.active || !pressed)
    {
        return;
    }

    int32_t text_len = static_cast<int32_t>(text_input_state_.text.length());
    if ((text_input_state_.selection_base < 0) ||
        (text_input_state_.selection_base > text_len))
    {
        text_input_state_.selection_base = text_len;
    }

    if ((text_input_state_.selection_extent < 0) ||
        (text_input_state_.selection_extent > text_len))
    {
        text_input_state_.selection_extent = text_input_state_.selection_base;
    }

    bool ctrl_active  = input_manager_ && input_manager_->is_ctrl_active();
    bool shift_active = input_manager_ && input_manager_->is_shift_active();

    if (ctrl_active)
    {
        if ((keysym == XKB_KEY_c) || (keysym == XKB_KEY_C))
        {
            if (text_input_state_.selection_base !=
                text_input_state_.selection_extent)
            {
                int32_t start = std::min(text_input_state_.selection_base,
                    text_input_state_.selection_extent);
                int32_t end = std::max(text_input_state_.selection_base,
                    text_input_state_.selection_extent);
                std::string sel = text_input_state_.text.substr(start, end - start);
                clipboard_text_ = sel;
                if (window_)
                {
                    window_->set_clipboard_text(sel);
                }
            }

            return;
        }

        if ((keysym == XKB_KEY_x) || (keysym == XKB_KEY_X))
        {
            if (text_input_state_.selection_base !=
                text_input_state_.selection_extent)
            {
                int32_t start = std::min(text_input_state_.selection_base,
                    text_input_state_.selection_extent);
                int32_t end = std::max(text_input_state_.selection_base,
                    text_input_state_.selection_extent);
                std::string sel = text_input_state_.text.substr(start, end - start);
                clipboard_text_ = sel;
                if (window_)
                {
                    window_->set_clipboard_text(sel);
                }

                text_input_state_.delete_selection();
                send_editing_state();
            }

            return;
        }

        if ((keysym == XKB_KEY_v) || (keysym == XKB_KEY_V))
        {
            std::string paste = clipboard_text_;
            if (window_)
            {
                paste = window_->get_clipboard_text();
                clipboard_text_ = paste;
            }

            if (!paste.empty())
            {
                text_input_state_.delete_selection();
                text_input_state_.text.insert(text_input_state_.selection_base, paste);
                text_input_state_.selection_base +=
                    static_cast<int32_t>(paste.length());
                text_input_state_.selection_extent = text_input_state_.selection_base;
                send_editing_state();
            }

            return;
        }

        if ((keysym == XKB_KEY_a) || (keysym == XKB_KEY_A))
        {
            text_input_state_.selection_base   = 0;
            text_input_state_.selection_extent =
                static_cast<int32_t>(text_input_state_.text.length());
            send_editing_state();
            return;
        }
    }

    bool changed = false;

    switch (keysym)
    {
      case XKB_KEY_BackSpace:
        if (text_input_state_.selection_base !=
            text_input_state_.selection_extent)
        {
            text_input_state_.delete_selection();
            changed = true;
        } else if (text_input_state_.selection_base > 0)
        {
            int32_t pos = text_input_state_.selection_base - 1;
            while ((pos > 0) &&
                   ((static_cast<unsigned char>(text_input_state_.text[pos]) &
                     0xC0) == 0x80))
            {
                pos--;
            }

            text_input_state_.text.erase(pos, text_input_state_.selection_base - pos);
            text_input_state_.selection_base   = pos;
            text_input_state_.selection_extent = pos;
            changed = true;
        }

        break;

      case XKB_KEY_Delete:
        if (text_input_state_.selection_base !=
            text_input_state_.selection_extent)
        {
            text_input_state_.delete_selection();
            changed = true;
        } else if (text_input_state_.selection_base <
                   static_cast<int32_t>(text_input_state_.text.length()))
        {
            int32_t pos = text_input_state_.selection_base + 1;
            while ((pos < static_cast<int32_t>(text_input_state_.text.length())) &&
                   ((static_cast<unsigned char>(text_input_state_.text[pos]) &
                     0xC0) == 0x80))
            {
                pos++;
            }

            text_input_state_.text.erase(text_input_state_.selection_base,
                pos - text_input_state_.selection_base);
            changed = true;
        }

        break;

      case XKB_KEY_Left:
        if (text_input_state_.selection_extent > 0)
        {
            int32_t pos = text_input_state_.selection_extent - 1;
            while ((pos > 0) &&
                   ((static_cast<unsigned char>(text_input_state_.text[pos]) &
                     0xC0) == 0x80))
            {
                pos--;
            }

            text_input_state_.selection_extent = pos;
            if (!shift_active)
            {
                text_input_state_.selection_base = pos;
            }

            changed = true;
        }

        break;

      case XKB_KEY_Right:
        if (text_input_state_.selection_extent <
            static_cast<int32_t>(text_input_state_.text.length()))
        {
            int32_t pos = text_input_state_.selection_extent + 1;
            while ((pos < static_cast<int32_t>(text_input_state_.text.length())) &&
                   ((static_cast<unsigned char>(text_input_state_.text[pos]) &
                     0xC0) == 0x80))
            {
                pos++;
            }

            text_input_state_.selection_extent = pos;
            if (!shift_active)
            {
                text_input_state_.selection_base = pos;
            }

            changed = true;
        }

        break;

      case XKB_KEY_Home:
        text_input_state_.selection_extent = 0;
        if (!shift_active)
        {
            text_input_state_.selection_base = 0;
        }

        changed = true;
        break;

      case XKB_KEY_End:
        text_input_state_.selection_extent =
            static_cast<int32_t>(text_input_state_.text.length());
        if (!shift_active)
        {
            text_input_state_.selection_base = text_input_state_.selection_extent;
        }

        changed = true;
        break;

      case XKB_KEY_Return:
      case XKB_KEY_KP_Enter:
        if (text_input_state_.multiline)
        {
            text_input_state_.delete_selection();
            text_input_state_.text.insert(text_input_state_.selection_base, "\n");
            text_input_state_.selection_base++;
            text_input_state_.selection_extent = text_input_state_.selection_base;
            changed = true;
        } else
        {
            perform_action(text_input_state_.input_action.empty() ?
                "TextInputAction.done" :
                text_input_state_.input_action);
        }

        break;

      case XKB_KEY_Tab:
        break;

      default:
        if ((unicode >= 0x20) && (unicode != 0x7F))
        {
            char utf8[8] = {};
            int len = 0;
            if (unicode < 0x80)
            {
                utf8[0] = static_cast<char>(unicode);
                len     = 1;
            } else if (unicode < 0x800)
            {
                utf8[0] = static_cast<char>(0xC0 | (unicode >> 6));
                utf8[1] = static_cast<char>(0x80 | (unicode & 0x3F));
                len     = 2;
            } else if (unicode < 0x10000)
            {
                utf8[0] = static_cast<char>(0xE0 | (unicode >> 12));
                utf8[1] = static_cast<char>(0x80 | ((unicode >> 6) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | (unicode & 0x3F));
                len     = 3;
            } else
            {
                utf8[0] = static_cast<char>(0xF0 | (unicode >> 18));
                utf8[1] = static_cast<char>(0x80 | ((unicode >> 12) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | ((unicode >> 6) & 0x3F));
                utf8[3] = static_cast<char>(0x80 | (unicode & 0x3F));
                len     = 4;
            }

            text_input_state_.delete_selection();
            text_input_state_.text.insert(text_input_state_.selection_base, utf8,
                len);
            text_input_state_.selection_base  += len;
            text_input_state_.selection_extent = text_input_state_.selection_base;
            changed = true;
        }

        break;
    }

    if (changed)
    {
        send_editing_state();
    }
}

void FlutterRunner::send_editing_state()
{
    if (!text_input_state_.active || !text_input_channel_)
    {
        return;
    }

    flutter::EncodableMap state;
    state[flutter::EncodableValue("text")] = flutter::EncodableValue(text_input_state_.text);
    state[flutter::EncodableValue("selectionBase")] =
        flutter::EncodableValue(text_input_state_.selection_base);
    state[flutter::EncodableValue("selectionExtent")] =
        flutter::EncodableValue(text_input_state_.selection_extent);
    state[flutter::EncodableValue("selectionAffinity")] = flutter::EncodableValue("TextAffinity.downstream");
    state[flutter::EncodableValue("selectionIsDirectional")] = flutter::EncodableValue(false);
    state[flutter::EncodableValue("composingBase")] =
        flutter::EncodableValue(text_input_state_.composing_base);
    state[flutter::EncodableValue("composingExtent")] =
        flutter::EncodableValue(text_input_state_.composing_extent);

    flutter::EncodableList list;
    list.push_back(flutter::EncodableValue(static_cast<int64_t>(text_input_state_.client_id)));
    list.push_back(flutter::EncodableValue(std::move(state)));

    text_input_channel_->InvokeMethod(
        "TextInputClient.updateEditingState",
        std::make_unique<flutter::EncodableValue>(std::move(list)));
}

void FlutterRunner::perform_action(const std::string & action)
{
    if (!text_input_state_.active || !text_input_channel_)
    {
        return;
    }

    flutter::EncodableList list;
    list.push_back(flutter::EncodableValue(static_cast<int64_t>(text_input_state_.client_id)));
    list.push_back(flutter::EncodableValue(action));

    text_input_channel_->InvokeMethod(
        "TextInputClient.performAction",
        std::make_unique<flutter::EncodableValue>(std::move(list)));
}
