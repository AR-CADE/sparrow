#ifndef FLUTTER_RUNNER_HPP
#define FLUTTER_RUNNER_HPP

#include <cstdint>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

#include "flutter_embedder.h"
#include <vulkan/vulkan.h>
#include "client_wrapper/binary_messenger.hpp"
#include "client_wrapper/incoming_message_dispatcher.hpp"
#include "client_wrapper/method_channel.h"
#include "client_wrapper/json_method_codec.h"
#include "client_wrapper/standard_method_codec.h"

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <flutter/platform/runner/pigeon/messages.h>

#include "ipc_client.hpp"

enum class RendererBackend
{
    kOpenGL,
    kVulkan,
};

class WaylandWindow;
class InputManager;
class VulkanWindow;

class FlutterRunner
{
    static std::atomic<FlutterRunner*> _instance;
    static std::mutex m_;

  public:
    static FlutterRunner *instance();
    FlutterRunner(WaylandWindow *window, InputManager *input_manager,
        RendererBackend backend   = RendererBackend::kOpenGL,
        bool enable_vk_validation = false,
        VkPresentModeKHR preferred_present_mode = VK_PRESENT_MODE_FIFO_KHR);
    ~FlutterRunner();

    bool init(const std::string & assets_path,
        const std::string & icu_data_path,
        const std::string & aot_elf_path = "",
        const std::vector<std::string> & engine_args = {});

    void shutdown();
    void process_tasks();
    void send_window_metrics(int32_t width, int32_t height, double pixel_ratio);

    FlutterEngine get_engine() const
    {
        return engine_;
    }

    BinaryMessenger * get_messenger()
    {
        return &messenger_;
    }

    WaylandWindow *get_window()
    {
        return window_;
    }

    class IpcClient * get_ipc_client()
    {
        return &ipc_client_;
    }

    void on_platform_message(const FlutterPlatformMessage *message);

  private:
    void init_platform_channels();
    void handle_text_input_key(xkb_keysym_t keysym, uint32_t unicode, bool pressed);
    void send_editing_state();
    void perform_action(const std::string & action);

    struct QueuedTask
    {
        FlutterTask task;
        uint64_t target_time_nanos;
    };

    struct TextInputState
    {
        bool active = false;
        int64_t client_id = 0;
        std::string text;
        int32_t selection_base   = 0;
        int32_t selection_extent = 0;
        int32_t composing_base   = -1;
        int32_t composing_extent = -1;
        std::string input_action = "TextInputAction.done";
        bool multiline = false;

        void delete_selection()
        {
            if (selection_base == selection_extent)
            {
                return;
            }

            int32_t start = std::min(selection_base, selection_extent);
            int32_t end   = std::max(selection_base, selection_extent);
            if ((start >= 0) && (end <= static_cast<int32_t>(text.length())) && (start < end))
            {
                text.erase(start, end - start);
                selection_base = selection_extent = start;
            }
        }
    };

    RendererBackend backend_   = RendererBackend::kOpenGL;
    bool enable_vk_validation_ = false;
    VkPresentModeKHR preferred_present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    std::unique_ptr<VulkanWindow> vulkan_window_;
    WaylandWindow *window_;
    InputManager *input_manager_ = nullptr;

    FlutterEngine engine_ = nullptr;
    FlutterEngineAOTData aot_data_ = nullptr;
    FlutterEngineProcTable embedder_api_ = {};

    BinaryMessenger messenger_;
    std::unique_ptr<IncomingMessageDispatcher> message_dispatcher_;

    std::unique_ptr<flutter::MethodChannel<rapidjson::Document>> platform_channel_;
    std::unique_ptr<flutter::MethodChannel<rapidjson::Document>> text_input_channel_;
    std::unique_ptr<sparrow::RunnerHostApi> runner_host_api_;
    std::unique_ptr<sparrow::RunnerHostIpcApi> runner_host_ipc_api_;
    std::unique_ptr<sparrow::RunnerFlutterIpcApi> runner_flutter_ipc_api_;
    // std::unique_ptr<sparrow::RunnerFlutterApi> runner_flutter_api;
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> mouse_cursor_channel_;

    std::mutex task_mutex_;
    std::vector<QueuedTask> queued_tasks_;
    FlutterTaskRunnerDescription platform_task_runner_ = {};
    FlutterCustomTaskRunners custom_task_runners_ = {};
    pthread_t main_thread_id_ = 0;

    std::string clipboard_text_;
    TextInputState text_input_state_;
    class IpcClient ipc_client_;
};

#endif // FLUTTER_RUNNER_HPP
