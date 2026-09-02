#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <linux/limits.h>
#include <optional>
#include <signal.h>

#include <sparrow/nonstd/wlroots-full.hpp>

#include "core.hpp"
#include "sparrow/options.hpp"

#include <execinfo.h>
#include <unistd.h>

#if defined (__has_feature)
    #if __has_feature(address_sanitizer)
        #define HAS_ASAN 1
    #endif
    #if __has_feature(thread_sanitizer)
        #define HAS_TSAN 1
    #endif
#endif
#if defined (__SANITIZE_ADDRESS__)
    #define HAS_ASAN 1
#endif
#if defined (__SANITIZE_THREAD__)
    #define HAS_TSAN 1
#endif

#if defined (HAS_ASAN)
extern "C" const char *__lsan_default_suppressions()
{
    return "leak:libgallium\n"
           "leak:libLLVM\n"
           "leak:libEGL_mesa\n"
           "leak:libfontconfig\n"
           "leak:libflutter_engine\n"
           "leak:libdrm\n"
           "leak:libglapi\n"
           "leak:libxkbcommon\n"
           "leak:libgbm\n";
}

#endif

#if defined (HAS_TSAN)
extern "C" const char *__tsan_default_suppressions()
{
    return "race:libgallium\n"
           "race:libEGL_mesa\n"
           "race:libdrm\n"
           "race:libfontconfig\n"
           "race:libflutter_engine\n";
}

#endif

std::optional<int> exit_because_signal;

static void term_signal_handler(int sig)
{
    exit_because_signal = sig;
    auto core = Core::instance();
    if (core && core->wl_display)
    {
        wl_display_terminate(core->wl_display);
    }
}

#if defined (PRINT_TRACE) && !defined (HAS_ASAN) && !defined (HAS_TSAN)
static void crash_signal_handler(int sig)
{
    const char *error = "Unknown fatal error";
    switch (sig)
    {
      case SIGSEGV:
        error = "Segmentation fault (SIGSEGV)";
        break;

      case SIGFPE:
        error = "Floating-point exception (SIGFPE)";
        break;

      case SIGABRT:
        error = "Fatal error (SIGABRT)";
        break;

      case SIGBUS:
        error = "Bus error (SIGBUS)";
        break;

      case SIGILL:
        error = "Illegal instruction (SIGILL)";
        break;
    }

    wlr_log(WLR_ERROR, "Fatal crash: %s", error);
    fprintf(stderr,
        "\n=======================================================\n");
    fprintf(stderr, " Sparrow Crash Caught: %s (signal %d)\n", error, sig);
    fprintf(stderr, " Backtrace / Call Stack:\n");
    fprintf(stderr, "=======================================================\n");
    void *callstack[64];
    int frames = backtrace(callstack, 64);
    backtrace_symbols_fd(callstack, frames, STDERR_FILENO);
    fprintf(stderr,
        "=======================================================\n\n");
    fflush(stderr);

    ::signal(sig, SIG_DFL);
    raise(sig);
}

#endif

#define MAX_ENGINE_ARGS 32

// ... existing code ...

std::string get_exec_path()
{
    char path[PATH_MAX];
    {
        ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);

        if (len != -1)
        {
            path[len] = '\0';
        } else
        {
            return "";
        }
    }

    const char *last_slash = strrchr(path, '/');
    if (!last_slash)
    {
        return "./";
    }

    size_t len = last_slash - path + 1;
    std::string res(path, len);
    return res;
}

int main(int argc, const char *argv[])
{
    const char *engine_argv[MAX_ENGINE_ARGS];
    int engine_argc = 0;

    engine_argv[engine_argc++] = argv[0];

    for (int i = 1; i < argc && engine_argc < MAX_ENGINE_ARGS; i++)
    {
        engine_argv[engine_argc++] = argv[i];
    }

    static std::vector<std::string> dynamic_engine_args;
    const char *switch_count_str = getenv("FLUTTER_ENGINE_SWITCHES");
    if (switch_count_str)
    {
        int switch_count = atoi(switch_count_str);
        for (int i = 1; i <= switch_count && engine_argc < MAX_ENGINE_ARGS; i++)
        {
            char env_name[64];
            snprintf(env_name, sizeof(env_name), "FLUTTER_ENGINE_SWITCH_%d", i);
            const char *switch_value = getenv(env_name);
            if (switch_value)
            {
                dynamic_engine_args.push_back(std::string("--") + switch_value);
                const char *arg = dynamic_engine_args.back().c_str();
                engine_argv[engine_argc++] = arg;
                printf("Adding engine switch: %s\n", arg);
            }
        }
    }

#ifdef ENABLE_IMPELLER
    bool has_impeller_flag = false;
    for (int i = 0; i < engine_argc; i++)
    {
        if (strstr(engine_argv[i], "enable-impeller") != nullptr)
        {
            has_impeller_flag = true;
            break;
        }
    }

    if (!has_impeller_flag && (engine_argc < MAX_ENGINE_ARGS))
    {
        engine_argv[engine_argc++] = "--enable-impeller=true";
        printf("Impeller build: Auto-adding --enable-impeller=true\n");
    }

#endif

    bool enable_vm_service = false;
    std::string vm_service_port = "8181";

    const char *env_vm_service = getenv("SPARROW_VM_SERVICE");
    if (env_vm_service != nullptr)
    {
        enable_vm_service = true;
        if ((strlen(env_vm_service) > 0) && (strcmp(env_vm_service, "1") != 0))
        {
            vm_service_port = env_vm_service;
        }
    }

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--vm-service") == 0)
        {
            enable_vm_service = true;
        } else if (strncmp(argv[i], "--vm-service=", 13) == 0)
        {
            enable_vm_service = true;
            vm_service_port   = argv[i] + 13;
        } else if (strncmp(argv[i], "--observatory-port=", 19) == 0)
        {
            enable_vm_service = true;
            vm_service_port   = argv[i] + 19;
        }
    }

    if (enable_vm_service && (engine_argc + 4 < MAX_ENGINE_ARGS))
    {
        dynamic_engine_args.push_back("--enable-vm-service=" + vm_service_port);
        engine_argv[engine_argc++] = dynamic_engine_args.back().c_str();
        dynamic_engine_args.push_back("--vm-service-port=" + vm_service_port);
        engine_argv[engine_argc++] = dynamic_engine_args.back().c_str();
        dynamic_engine_args.push_back("--observatory-port=" + vm_service_port);
        engine_argv[engine_argc++] = dynamic_engine_args.back().c_str();
        dynamic_engine_args.push_back("--disable-service-auth-codes");
        engine_argv[engine_argc++] = dynamic_engine_args.back().c_str();
        printf("[VM_SERVICE] Enabling Dart VM Service on port %s\n",
            vm_service_port.c_str());
    }

    struct sparrow_options opts = {};
    opts.argc = engine_argc;
    opts.argv = engine_argv;

    const char *assets_path   = getenv("SPARROW_ASSETS_PATH");
    const char *icu_data_path = getenv("SPARROW_ICU_PATH");
    const char *elf_file_path = getenv("SPARROW_ELF_PATH");

    std::string path = get_exec_path();

    if (!assets_path && !path.empty())
    {
        opts.assets_path = path + "shell/data/flutter_assets";
        if (!std::filesystem::exists(opts.assets_path))
        {
            opts.assets_path = path + "data/flutter_assets";
        }
    } else if (assets_path)
    {
        opts.assets_path = assets_path;
    } else
    {
        return 1;
    }

    if (!std::filesystem::exists(opts.assets_path))
    {
        return 1;
    }

    if (!icu_data_path && !path.empty())
    {
        opts.icu_data_path = path + "shell/data/icudtl.dat";
        if (!std::filesystem::exists(opts.icu_data_path))
        {
            opts.icu_data_path = path + "data/icudtl.dat";
        }
    } else if (icu_data_path)
    {
        opts.icu_data_path = icu_data_path;
    } else
    {
        return 1;
    }

    if (!std::filesystem::exists(opts.icu_data_path))
    {
        return 1;
    }

    if (!elf_file_path && !path.empty())
    {
        opts.elf_file_path = path + "shell/app.so";
        if (!std::filesystem::exists(opts.elf_file_path))
        {
            opts.elf_file_path = path + "app.so";
        }
    } else if (elf_file_path)
    {
        opts.elf_file_path = elf_file_path;
    } else
    {
        return 1;
    }

    if (!std::filesystem::exists(opts.elf_file_path))
    {
        return 1;
    }

    printf("%s\n", opts.assets_path.c_str());
    printf("%s\n", opts.icu_data_path.c_str());
    printf("%s\n", opts.elf_file_path.c_str());

    bool allow_root = false;

    for (int i = 1; i < argc; i++)
    {
        if ((strcmp(argv[i], "--debug-damage") == 0) ||
            (strcmp(argv[i], "-D") == 0))
        {
            setenv("SPARROW_DEBUG_DAMAGE", "1", 1);
        }

        if ((strcmp(argv[i], "--fps") == 0) || (strcmp(argv[i], "-F") == 0))
        {
            setenv("SPARROW_SHOW_FPS", "1", 1);
        }

        if ((strcmp(argv[i], "--debug-protocol") == 0) ||
            (strcmp(argv[i], "-P") == 0))
        {
            setenv("SPARROW_DEBUG_PROTOCOL", "1", 1);
        }

        if ((strcmp(argv[i], "--verbose") == 0) || (strcmp(argv[i], "-v") == 0))
        {
            setenv("SPARROW_DEBUG", "1", 1);
        }

        if ((strcmp(argv[i], "--inspect") == 0) || (strcmp(argv[i], "-I") == 0))
        {
            setenv("SPARROW_INSPECT", "1", 1);
        }

        if ((strcmp(argv[i], "--trace-gpu") == 0) || (strcmp(argv[i], "-G") == 0))
        {
            setenv("SPARROW_TRACE_GPU", "1", 1);
        }

        if (strcmp(argv[i], "--trace") == 0)
        {
            setenv("SPARROW_TRACE", "1", 1);
        }

        if (strncmp(argv[i], "--buffering=", 12) == 0)
        {
            setenv("SPARROW_BUFFERING", argv[i] + 12, 1);
        }

        if ((strcmp(argv[i], "--triple-buffer") == 0) ||
            (strcmp(argv[i], "-3") == 0))
        {
            setenv("SPARROW_BUFFERING", "triple", 1);
        }

        if (strcmp(argv[i], "--auto-buffer") == 0)
        {
            setenv("SPARROW_BUFFERING", "auto", 1);
        }

        if ((strcmp(argv[i], "--double-buffer") == 0) ||
            (strcmp(argv[i], "-2") == 0))
        {
            setenv("SPARROW_BUFFERING", "double", 1);
        }

        if ((strcmp(argv[i], "--no-realtime") == 0) ||
            (strcmp(argv[i], "-n") == 0))
        {
            setenv("SPARROW_NO_REALTIME", "1", 1);
        }
    }

    if (!getenv("XDG_CURRENT_DESKTOP"))
    {
        setenv("XDG_CURRENT_DESKTOP", "wayfire", 1);
    }

    /* Don't crash on SIGPIPE, e.g., when doing IPC to a client whose fd has been closed. */
    signal(SIGPIPE, SIG_IGN);

    unsetenv("DISPLAY");

#if defined (PRINT_TRACE) && !defined (HAS_ASAN) && !defined (HAS_TSAN)
    /* In case of crash, print the stacktrace for debugging. However, if ASAN or TSAN is enabled, their
     * built-in crash handlers provide much richer diagnostics. */
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGBUS, crash_signal_handler);
    signal(SIGILL, crash_signal_handler);
#endif

    signal(SIGINT, term_signal_handler);
    signal(SIGTERM, term_signal_handler);

    std::set_terminate([] ()
    {
        std::cout << "Unhandled exception" << std::endl;
        std::abort();
    });

    return Core::instance()->init(opts, allow_root);
}
