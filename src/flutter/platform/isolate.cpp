#include "flutter/platform/isolate.hpp"
#include "core.hpp"

#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <sstream>
#include <iomanip>
#include <sparrow/nonstd/wlroots-full.hpp>

static std::mutex s_isolate_mutex;
static std::map<std::string, int64_t> s_port_map;

std::optional<int64_t> sparrow_isolate_lookup_port(const std::string& name)
{
    std::lock_guard<std::mutex> lock(s_isolate_mutex);
    auto it = s_port_map.find(name);
    if (it != s_port_map.end())
    {
        return it->second;
    }

    return std::nullopt;
}

bool sparrow_isolate_register_port(const std::string& name, int64_t port_id)
{
    std::lock_guard<std::mutex> lock(s_isolate_mutex);
    if (s_port_map.find(name) != s_port_map.end())
    {
        return false;
    }

    s_port_map[name] = port_id;
    return true;
}

bool sparrow_isolate_remove_port(const std::string& name)
{
    std::lock_guard<std::mutex> lock(s_isolate_mutex);
    auto it = s_port_map.find(name);
    if (it != s_port_map.end())
    {
        s_port_map.erase(it);
        return true;
    }

    return false;
}

void sparrow_isolate_channel_init()
{
    Core *instance = Core::instance();

    instance->messenger.SetMessageHandler(
        "flutter/isolate",
        [] (const uint8_t *message, size_t message_size, BinaryReply reply)
    {
        if (!message || (message_size == 0))
        {
            if (reply)
            {
                reply(nullptr, 0);
            }

            return;
        }

        std::string msg_str(reinterpret_cast<const char*>(message), message_size);
        std::ostringstream hex_dump;
        for (size_t i = 0; i < message_size && i < 32; ++i)
        {
            hex_dump << std::hex << std::setw(2) << std::setfill('0') << (int)message[i] << " ";
        }

        wlr_log(WLR_INFO, "[ISOLATE_RAW] Received on flutter/isolate (%zu bytes): hex=[%s] str=[%s]",
            message_size, hex_dump.str().c_str(), msg_str.c_str());

        // Reply empty/success
        if (reply)
        {
            uint8_t success_byte = 1;
            reply(&success_byte, 1);
        }
    });

    wlr_log(WLR_INFO, "[ISOLATE] flutter/isolate raw binary message handler registered");
}
