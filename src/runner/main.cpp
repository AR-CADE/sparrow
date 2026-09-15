#include "flutter_runner.hpp"
#include "input_manager.hpp"
#include "wayland_window.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/prctl.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

#if defined (__has_feature)
    #if __has_feature(address_sanitizer)
        #define RUNNER_HAS_ASAN 1
    #endif
    #if __has_feature(thread_sanitizer)
        #define RUNNER_HAS_TSAN 1
    #endif
    #if __has_feature(undefined_behavior_sanitizer)
        #define RUNNER_HAS_UBSAN 1
    #endif
#endif
#if defined (__SANITIZE_ADDRESS__) || defined (HAS_ASAN)
    #define RUNNER_HAS_ASAN 1
#endif
#if defined (__SANITIZE_THREAD__) || defined (HAS_TSAN)
    #define RUNNER_HAS_TSAN 1
#endif
#if defined (__SANITIZE_UNDEFINED__) || defined (HAS_UBSAN)
    #define RUNNER_HAS_UBSAN 1
#endif

#ifdef RUNNER_HAS_TSAN
extern "C" const char * __tsan_default_options()
{
    return "suppressions=tsan_suppressions.txt";
}

#endif

#ifdef RUNNER_HAS_ASAN
extern "C" const char * __lsan_default_options()
{
    return "suppressions=lsan_suppressions.txt";
}

#endif

#ifdef RUNNER_HAS_UBSAN
extern "C" const char * __ubsan_default_options()
{
    return "suppressions=ubsan_suppressions.txt";
}

#endif

static volatile std::sig_atomic_t g_exit_requested = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_exit_requested = 1;
}

static void print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTIONS] [BUNDLE_DIR_OR_ASSETS_PATH]\n\n", prog_name);
    printf("Minimalist native Wayland application runner and embedder for Flutter\n\n");
    printf("Options:\n");
    printf("  --bundle=<path>         Path to Flutter application bundle directory\n");
    printf("  --assets-path=<path>    Path to flutter_assets directory\n");
    printf("  --icu-data-path=<path>  Path to icudtl.dat file\n");
    printf("  --aot-elf-path=<path>   Path to AOT compiled ELF (app.so or libapp.so)\n");
    printf("  --app-id=<id>           Wayland xdg_toplevel app_id (default: sparrow.flutter.app)\n");
    printf("  --title=<title>         Window title (default: Flutter Application)\n");
    printf("  --width=<pixels>        Initial window width (default: 1280)\n");
    printf("  --height=<pixels>       Initial window height (default: 720)\n");
    printf("  --fullscreen            Start in fullscreen mode\n");
    printf("  --maximized             Start maximized\n");
    printf("  --vulkan                Use native Vulkan renderer instead of OpenGL\n");
    printf("  --vk-validation         Enable Vulkan Khronos validation layers (implies --vulkan)\n");
    printf("  --vk-present-mode=<mode> Swapchain present mode: fifo (default), mailbox, immediate\n");
    printf("  -h, --help              Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s out/shell\n", prog_name);
    printf("  %s --bundle=my_app/build/linux/x64/release/bundle\n", prog_name);
    printf("  %s --assets-path=my_app/flutter_assets --icu-data-path=icudtl.dat\n", prog_name);
}

int main(int argc, char *argv[])
{
#ifdef PR_SET_DUMPABLE
    prctl(PR_SET_DUMPABLE, 1);
#endif
#ifdef PR_SET_PTRACER
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
#endif

    std::string bundle_path;
    std::string assets_path;
    std::string icu_data_path;
    std::string aot_elf_path;
    std::string app_id = "sparrow.flutter.app";
    std::string title  = "Flutter Application";
    int32_t width   = 1280;
    int32_t height  = 720;
    bool fullscreen = false;
    bool maximized  = false;
    bool use_vulkan = false;
    bool vk_validation = false;
    VkPresentModeKHR vk_present_mode = VK_PRESENT_MODE_FIFO_KHR;
    int ipc_fd = -1;
    std::vector<std::string> engine_args;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "-h") || (arg == "--help"))
        {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg.rfind("--bundle=", 0) == 0)
        {
            bundle_path = arg.substr(9);
        } else if (arg.rfind("--assets-path=", 0) == 0)
        {
            assets_path = arg.substr(14);
        } else if (arg.rfind("--icu-data-path=", 0) == 0)
        {
            icu_data_path = arg.substr(16);
        } else if (arg.rfind("--aot-elf-path=", 0) == 0)
        {
            aot_elf_path = arg.substr(15);
        } else if (arg.rfind("--app-id=", 0) == 0)
        {
            app_id = arg.substr(9);
        } else if (arg.rfind("--title=", 0) == 0)
        {
            title = arg.substr(8);
        } else if (arg.rfind("--width=", 0) == 0)
        {
            width = static_cast<int>(std::strtol(arg.substr(8).c_str(), nullptr, 10));
        } else if (arg.rfind("--height=", 0) == 0)
        {
            height = static_cast<int>(std::strtol(arg.substr(9).c_str(), nullptr, 10));
        } else if (arg == "--fullscreen")
        {
            fullscreen = true;
        } else if (arg == "--maximized")
        {
            maximized = true;
        } else if (arg == "--vulkan")
        {
            use_vulkan = true;
        } else if (arg == "--vk-validation")
        {
            vk_validation = true;
            use_vulkan    = true;
        } else if (arg.rfind("--vk-present-mode=", 0) == 0)
        {
            std::string mode_str = arg.substr(18);
            if (mode_str == "mailbox")
            {
                vk_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            } else if (mode_str == "immediate")
            {
                vk_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            } else if (mode_str == "fifo_relaxed")
            {
                vk_present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            } else
            {
                vk_present_mode = VK_PRESENT_MODE_FIFO_KHR;
            }

            use_vulkan = true;
        } else if (arg.rfind("--sparrow-ipc-fd=", 0) == 0)
        {
            ipc_fd = static_cast<int>(std::strtol(arg.substr(17).c_str(), nullptr, 10));
        } else if (arg.rfind("--", 0) == 0)
        {
            engine_args.push_back(arg);
        } else if (bundle_path.empty())
        {
            bundle_path = arg;
        }
    }

    // Auto-detect bundle assets if bundle_path is provided
    if (!bundle_path.empty())
    {
        if (assets_path.empty())
        {
            if (fs::exists(fs::path(bundle_path) / "flutter_assets"))
            {
                assets_path = (fs::path(bundle_path) / "flutter_assets").string();
            } else if (fs::exists(fs::path(bundle_path) / "data" / "flutter_assets"))
            {
                assets_path = (fs::path(bundle_path) / "data" / "flutter_assets").string();
            }
        }

        if (icu_data_path.empty())
        {
            if (fs::exists(fs::path(bundle_path) / "icudtl.dat"))
            {
                icu_data_path = (fs::path(bundle_path) / "icudtl.dat").string();
            } else if (fs::exists(fs::path(bundle_path) / "data" / "icudtl.dat"))
            {
                icu_data_path = (fs::path(bundle_path) / "data" / "icudtl.dat").string();
            }
        }

        if (aot_elf_path.empty())
        {
            if (fs::exists(fs::path(bundle_path) / "lib" / "libapp.so"))
            {
                aot_elf_path = (fs::path(bundle_path) / "lib" / "libapp.so").string();
            } else if (fs::exists(fs::path(bundle_path) / "app.so"))
            {
                aot_elf_path = (fs::path(bundle_path) / "app.so").string();
            }
        }
    }

    // Fallback standard locations for icudtl.dat
    if (icu_data_path.empty())
    {
        const std::vector<std::string> search_paths = {
            "out/shell/icudtl.dat",
            "out/shell/data/icudtl.dat",
            "/usr/share/flutter/icudtl.dat"
        };
        for (const auto & sp : search_paths)
        {
            if (fs::exists(sp))
            {
                icu_data_path = sp;
                break;
            }
        }
    }

    if (assets_path.empty())
    {
        fprintf(stderr, "[sparrow-app-runner] Error: No flutter_assets directory found.\n");
        fprintf(stderr, "Please specify a bundle directory or --assets-path.\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (icu_data_path.empty())
    {
        fprintf(stderr, "[sparrow-app-runner] Error: icudtl.dat not found.\n");
        fprintf(stderr, "Please specify --icu-data-path.\n\n");
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    WaylandWindow window;
    InputManager input_manager(&window);

    window.on_seat_bound = [&input_manager] (struct wl_seat *seat)
    {
        input_manager.bind_seat(seat);
    };

    if (!window.init(app_id, title, width, height, fullscreen, maximized, !use_vulkan))
    {
        fprintf(stderr, "[sparrow-app-runner] Failed to initialize Wayland window\n");
        return EXIT_FAILURE;
    }

    if (!input_manager.init())
    {
        fprintf(stderr, "[sparrow-app-runner] Failed to initialize input manager\n");
        return EXIT_FAILURE;
    }

    FlutterRunner runner(&window, &input_manager,
        use_vulkan ? RendererBackend::kVulkan : RendererBackend::kOpenGL,
        vk_validation, vk_present_mode);
    if (!runner.init(assets_path, icu_data_path, aot_elf_path, engine_args))
    {
        fprintf(stderr, "[sparrow-app-runner] Failed to initialize Flutter runner\n");
        return EXIT_FAILURE;
    }

    if (ipc_fd >= 0)
    {
        runner.get_ipc_client()->set_fd(ipc_fd);
    }

    window.on_window_metrics_changed = [&runner] (int32_t w, int32_t h, double pr)
    {
        runner.send_window_metrics(w, h, pr);
    };

    window.on_close_requested = [&window] ()
    {
        window.request_close();
    };

    // Main event loop
    while (window.is_running() && !g_exit_requested)
    {
        runner.process_tasks();
        window.dispatch_events();
    }

    printf("[sparrow-app-runner] Exiting main loop...\n");
    fflush(stdout);
    runner.shutdown();
    printf("[sparrow-app-runner] Runner shutdown complete.\n");
    fflush(stdout);
    input_manager.shutdown();
    printf("[sparrow-app-runner] Input shutdown complete.\n");
    fflush(stdout);
    window.shutdown();
    printf("[sparrow-app-runner] Window shutdown complete.\n");
    fflush(stdout);

    _exit(EXIT_SUCCESS);
}
