#include "restoration.hpp"
#include "client_wrapper/encodable_value.h"
#include "client_wrapper/method_channel.h"
#include "client_wrapper/standard_method_codec.h"
#include "core.hpp"
#include <sparrow/nonstd/wlroots-full.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

static std::vector<uint8_t> g_restoration_data;
static bool g_restoration_dirty = false;
static std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
g_restoration_channel;

static std::string get_restoration_file_path()
{
    const char *xdg_cache_home = getenv("XDG_CACHE_HOME");
    std::string base_dir;
    if ((xdg_cache_home != nullptr) && (strlen(xdg_cache_home) > 0))
    {
        base_dir = std::string(xdg_cache_home) + "/sparrow";
    } else
    {
        const char *home = getenv("HOME");
        base_dir = ((home != nullptr) && (strlen(home) > 0)) ?
            (std::string(home) + "/.cache/sparrow") :
            "/tmp/sparrow";
    }

    return base_dir + "/session_state.bin";
}

// Must match the storage directory computed in shell/lib/main.dart.
static std::vector<std::string> get_shell_storage_dirs()
{
    std::vector<std::string> dirs;
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if ((xdg_data_home != nullptr) && (strlen(xdg_data_home) > 0))
    {
        dirs.push_back(std::string(xdg_data_home) + "/sparrow/shell_storage");
    }

    const char *home = getenv("HOME");
    if ((home != nullptr) && (strlen(home) > 0))
    {
        dirs.push_back(std::string(home) + "/.local/share/sparrow/shell_storage");
    }

    dirs.emplace_back("/tmp/sparrow/shell_storage");
    return dirs;
}

void sparrow_restoration_clear_persisted_state()
{
    g_restoration_data.clear();
    g_restoration_dirty = false;

    std::error_code ec;
    const std::string state_path = get_restoration_file_path();
    if (std::filesystem::remove(state_path, ec))
    {
        wlr_log(WLR_INFO, "[RESTORATION] Cold boot: removed stale %s",
            state_path.c_str());
    }

    for (const auto & dir : get_shell_storage_dirs())
    {
        ec.clear();
        const auto removed = std::filesystem::remove_all(dir, ec);
        if (ec)
        {
            wlr_log(WLR_ERROR, "[RESTORATION] Failed to remove %s: %s", dir.c_str(),
                ec.message().c_str());
        } else if ((removed != static_cast<std::uintmax_t>(-1)) && (removed > 0))
        {
            wlr_log(WLR_INFO,
                "[RESTORATION] Cold boot: removed stale shell storage %s",
                dir.c_str());
        }
    }
}

static void load_restoration_data_from_disk()
{
    std::string path = get_restoration_file_path();
    std::ifstream file(path, std::ios::binary);
    if (file.is_open())
    {
        file.seekg(0, std::ios::end);
        const std::streampos size = file.tellg();
        if (size > 0)
        {
            file.seekg(0, std::ios::beg);
            g_restoration_data.resize(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(g_restoration_data.data()), size);
            wlr_log(WLR_INFO, "[RESTORATION] Loaded %zu bytes from %s",
                g_restoration_data.size(), path.c_str());
            g_restoration_dirty = false;
        }
    }
}

void sparrow_restoration_flush_to_disk()
{
    if (!g_restoration_dirty || g_restoration_data.empty())
    {
        return;
    }

    const std::string path = get_restoration_file_path();
    const std::string dir  = std::filesystem::path(path).parent_path().string();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        wlr_log(WLR_ERROR, "[RESTORATION] Failed to create dir %s: %s", dir.c_str(),
            ec.message().c_str());
        return;
    }

    const std::string temp_path = path + ".tmp";
    std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        wlr_log(WLR_ERROR, "[RESTORATION] Failed to open %s for writing",
            temp_path.c_str());
        return;
    }

    file.write(reinterpret_cast<const char*>(g_restoration_data.data()),
        static_cast<std::streamsize>(g_restoration_data.size()));
    file.flush();
    file.close();

    if (rename(temp_path.c_str(), path.c_str()) != 0)
    {
        wlr_log(WLR_ERROR, "[RESTORATION] Failed to rename %s to %s",
            temp_path.c_str(), path.c_str());
    } else
    {
        wlr_log(WLR_INFO, "[RESTORATION] Flushed %zu bytes to %s",
            g_restoration_data.size(), path.c_str());
        g_restoration_dirty = false;
    }
}

const std::vector<uint8_t>&sparrow_restoration_get_data()
{
    return g_restoration_data;
}

void sparrow_restoration_set_data(const std::vector<uint8_t> & data)
{
    g_restoration_data  = data;
    g_restoration_dirty = true;
}

void sparrow_restoration_init()
{
    const auto instance = Core::instance();
    if (!instance)
    {
        return;
    }

    g_restoration_channel =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            &instance->messenger, "flutter/restoration",
            &flutter::StandardMethodCodec::GetInstance());

    g_restoration_channel->SetMethodCallHandler(
        [] (const flutter::MethodCall<flutter::EncodableValue> & call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
            result)
    {
        const std::string & method = call.method_name();

        if (method == "get")
        {
            if (g_restoration_data.empty())
            {
                load_restoration_data_from_disk();
            }

            flutter::EncodableMap map;
            map[flutter::EncodableValue("enabled")] =
                flutter::EncodableValue(true);
            if (!g_restoration_data.empty())
            {
                map[flutter::EncodableValue("data")] =
                    flutter::EncodableValue(g_restoration_data);
                wlr_log(WLR_INFO, "[RESTORATION] Replied to 'get' with %zu bytes",
                    g_restoration_data.size());
            } else
            {
                map[flutter::EncodableValue("data")] = flutter::EncodableValue();
                wlr_log(WLR_INFO, "[RESTORATION] Replied to 'get' with no "
                                  "restoration data (clean startup)");
            }

            result->Success(flutter::EncodableValue(map));
        } else if (method == "put")
        {
            const auto *args = call.arguments();
            if ((args != nullptr) &&
                std::holds_alternative<std::vector<uint8_t>>(*args))
            {
                const auto & blob   = std::get<std::vector<uint8_t>>(*args);
                g_restoration_data  = blob;
                g_restoration_dirty = true;
                wlr_log(WLR_DEBUG,
                    "[RESTORATION] Received 'put' (%zu bytes) - cached in RAM "
                    "(deferred flush)",
                    blob.size());
            }

            result->Success();
        } else
        {
            wlr_log(WLR_INFO, "Unhandled flutter/restoration method: %s",
                method.c_str());
            result->NotImplemented();
        }
    });

    wlr_log(WLR_INFO, "Restoration plugin initialized (flutter/restoration)");
}

void sparrow_restoration_reset()
{
    sparrow_restoration_flush_to_disk();
    g_restoration_channel.reset();
}
