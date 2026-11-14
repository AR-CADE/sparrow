#include "session_manifest.hpp"
#include "core.hpp"
#include "surface/view.hpp"
#include <sparrow/nonstd/wlroots-full.hpp>

#include <simdjson.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

struct SurfaceSessionEntry
{
    std::string app_id;
    std::string title;
    int x     = 0;
    int y     = 0;
    int width = 0;
    int height     = 0;
    bool maximized = false;
    bool fullscreen = false;
};

static std::unordered_map<std::string, SurfaceSessionEntry> g_saved_surfaces;

static std::string get_manifest_file_path()
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

    return base_dir + "/session_surfaces.json";
}

void sparrow_session_manifest_init()
{
    const std::string path = get_manifest_file_path();
    std::ifstream file(path);
    if (!file.is_open())
    {
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    if (content.empty())
    {
        return;
    }

    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    auto error = parser.parse(content).get(doc);
    if (error || !doc.is_array())
    {
        wlr_log(WLR_ERROR, "[SESSION] Failed to parse %s", path.c_str());
        return;
    }

    g_saved_surfaces.clear();
    for (auto obj : doc.get_array())
    {
        if (!obj.is_object())
        {
            continue;
        }

        SurfaceSessionEntry entry;
        std::string_view sv;
        if (obj["app_id"].get(sv) == simdjson::SUCCESS)
        {
            entry.app_id = std::string(sv);
        }

        if (obj["title"].get(sv) == simdjson::SUCCESS)
        {
            entry.title = std::string(sv);
        }

        int64_t val = 0;
        if (obj["x"].get(val) == simdjson::SUCCESS)
        {
            entry.x = (int)val;
        }

        if (obj["y"].get(val) == simdjson::SUCCESS)
        {
            entry.y = (int)val;
        }

        if (obj["width"].get(val) == simdjson::SUCCESS)
        {
            entry.width = (int)val;
        }

        if (obj["height"].get(val) == simdjson::SUCCESS)
        {
            entry.height = (int)val;
        }

        bool b = false;
        if (obj["maximized"].get(b) == simdjson::SUCCESS)
        {
            entry.maximized = b;
        }

        bool fs = false;
        if (obj["fullscreen"].get(fs) == simdjson::SUCCESS)
        {
            entry.fullscreen = fs;
        }

        if (!entry.app_id.empty())
        {
            g_saved_surfaces[entry.app_id] = entry;
        }
    }

    wlr_log(WLR_INFO, "[SESSION] Loaded %zu surface entries from %s",
        g_saved_surfaces.size(), path.c_str());
}

static std::string escape_json_string(const std::string & input)
{
    std::ostringstream ss;
    for (char c : input)
    {
        switch (c)
        {
          case '"':
            ss << "\\\"";
            break;

          case '\\':
            ss << "\\\\";
            break;

          case '\b':
            ss << "\\b";
            break;

          case '\f':
            ss << "\\f";
            break;

          case '\n':
            ss << "\\n";
            break;

          case '\r':
            ss << "\\r";
            break;

          case '\t':
            ss << "\\t";
            break;

          default:
            if (('\x00' <= c) && (c <= '\x1f'))
            {
                ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
            } else
            {
                ss << c;
            }
        }
    }

    return ss.str();
}

void sparrow_session_manifest_flush()
{
    const auto core = Core::instance();
    if (!core)
    {
        return;
    }

    std::ostringstream ss;
    ss << "[\n";
    bool first   = true;
    size_t count = 0;

    SparrowView *view = nullptr;
    wl_list_for_each(view, &core->views_list, link)
    {
        if (!view->toplevel || !view->toplevel->app_id)
        {
            continue;
        }

        const char *app_id = view->toplevel->app_id;
        const char *title  = view->toplevel->title ? view->toplevel->title : "";

        if (!first)
        {
            ss << ",\n";
        }

        first = false;
        count++;

        ss << "  {\n";
        ss << "    \"app_id\": \"" << escape_json_string(app_id) << "\",\n";
        ss << "    \"title\": \"" << escape_json_string(title) << "\",\n";
        ss << "    \"x\": " << view->x << ",\n";
        ss << "    \"y\": " << view->y << ",\n";
        ss << "    \"width\": " << view->width << ",\n";
        ss << "    \"height\": " << view->height << ",\n";
        ss << "    \"maximized\": " << (view->maximized ? "true" : "false")
           << ",\n";
        ss << "    \"fullscreen\": " << (view->fullscreen ? "true" : "false")
           << "\n";
        ss << "  }";
    }

    ss << "\n]\n";

    const std::string path = get_manifest_file_path();
    const std::string dir  = std::filesystem::path(path).parent_path().string();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        wlr_log(WLR_ERROR, "[SESSION] Failed to create dir %s: %s", dir.c_str(),
            ec.message().c_str());
        return;
    }

    const std::string temp_path = path + ".tmp";
    std::ofstream file(temp_path, std::ios::trunc);
    if (!file.is_open())
    {
        wlr_log(WLR_ERROR, "[SESSION] Failed to open %s for writing",
            temp_path.c_str());
        return;
    }

    file << ss.str();
    file.flush();
    file.close();

    if (rename(temp_path.c_str(), path.c_str()) != 0)
    {
        wlr_log(WLR_ERROR, "[SESSION] Failed to rename %s to %s", temp_path.c_str(),
            path.c_str());
    } else
    {
        wlr_log(WLR_INFO, "[SESSION] Flushed %zu active views to %s", count,
            path.c_str());
    }
}

bool sparrow_session_manifest_apply_for_view(SparrowView *view)
{
    if (!view || !view->toplevel || !view->toplevel->app_id)
    {
        return false;
    }

    const std::string app_id(view->toplevel->app_id);
    auto it = g_saved_surfaces.find(app_id);
    if (it == g_saved_surfaces.end())
    {
        return false;
    }

    const auto & entry = it->second;
    if ((entry.width > 0) && (entry.height > 0))
    {
        view->x     = entry.x;
        view->y     = entry.y;
        view->width = entry.width;
        view->height     = entry.height;
        view->maximized  = entry.maximized;
        view->fullscreen = entry.fullscreen;
        if (view->fullscreen)
        {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now_ms =
                (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
            view->fullscreen_active_since_ms = (now_ms > 2000) ? (now_ms - 2000) : 1;
        }

        wlr_log(WLR_INFO,
            "[SESSION] Restored topology for view '%s': (%d,%d) %dx%d "
            "(maximized=%d, fullscreen=%d)",
            app_id.c_str(), view->x, view->y, view->width, view->height,
            view->maximized, view->fullscreen);
        return true;
    }

    return false;
}
