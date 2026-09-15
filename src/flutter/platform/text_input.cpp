#include "text_input.hpp"
#include "client_wrapper/json_method_codec.h"
#include "client_wrapper/method_channel.h"
#include "core.hpp"

#include <rapidjson/document.h>
#include <xkbcommon/xkbcommon.h>
#include <sparrow/nonstd/wlroots-full.hpp>

#include <algorithm>
#include <string>

struct text_input_state
{
    bool active = false;
    int64_t connection_id = 0;

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
        text.erase(start, end - start);
        selection_base   = start;
        selection_extent = start;
    }
};

static struct text_input_state g_text_input = {};
static std::unique_ptr<flutter::MethodChannel<rapidjson::Document>> g_text_input_channel;
static std::string g_clipboard_text;

// Repeat timer state
static struct wl_event_source *g_repeat_timer = nullptr;
static uint32_t g_repeat_keycode = 0;
static xkb_keysym_t g_repeat_sym = XKB_KEY_NoSymbol;
static uint32_t g_repeat_unicode = 0;
static bool g_repeat_ctrl    = false;
static bool g_repeat_shift   = false;
static int32_t g_repeat_rate = 25;
// static int32_t g_repeat_delay = 300;
static bool g_repeat_active = false;

static void send_editing_state()
{
    if (!g_text_input.active || !g_text_input_channel)
    {
        return;
    }

    rapidjson::Document doc;
    auto& allocator = doc.GetAllocator();
    doc.SetArray();

    doc.PushBack(rapidjson::Value(static_cast<int64_t>(g_text_input.connection_id)), allocator);

    rapidjson::Value state(rapidjson::kObjectType);
    state.AddMember("text", rapidjson::Value(g_text_input.text.c_str(), allocator), allocator);
    state.AddMember("selectionBase", g_text_input.selection_base, allocator);
    state.AddMember("selectionExtent", g_text_input.selection_extent, allocator);
    state.AddMember("composingBase", g_text_input.composing_base, allocator);
    state.AddMember("composingExtent", g_text_input.composing_extent, allocator);

    doc.PushBack(state, allocator);

    g_text_input_channel->InvokeMethod("TextInputClient.updateEditingState",
        std::make_unique<rapidjson::Document>(std::move(doc)));
}

static void perform_action(const char *action)
{
    if (!g_text_input.active || !g_text_input_channel)
    {
        return;
    }

    rapidjson::Document doc;
    auto& allocator = doc.GetAllocator();
    doc.SetArray();

    doc.PushBack(rapidjson::Value(static_cast<int64_t>(g_text_input.connection_id)), allocator);
    doc.PushBack(rapidjson::Value(action, allocator), allocator);

    g_text_input_channel->InvokeMethod("TextInputClient.performAction",
        std::make_unique<rapidjson::Document>(std::move(doc)));
}

static int repeat_timer_callback(void *data)
{
    (void)data;
    if (!g_repeat_active || !g_text_input.active)
    {
        return 0;
    }

    sparrow_text_input_handle_key(g_repeat_sym, g_repeat_unicode, true, g_repeat_ctrl, g_repeat_shift);

    int interval_ms = (g_repeat_rate > 0) ? (1000 / g_repeat_rate) : 40;
    wl_event_source_timer_update(g_repeat_timer, interval_ms);
    return 0;
}

void sparrow_text_input_start_repeat(
    uint32_t keycode,
    xkb_keysym_t sym,
    uint32_t unicode,
    bool ctrl_active,
    bool shift_active,
    int32_t rate,
    int32_t delay)
{
    if (!g_repeat_timer || !g_text_input.active)
    {
        return;
    }

    if ((rate <= 0) || (delay <= 0))
    {
        return;
    }

    g_repeat_keycode = keycode;
    g_repeat_sym     = sym;
    g_repeat_unicode = unicode;
    g_repeat_ctrl    = ctrl_active;
    g_repeat_shift   = shift_active;
    g_repeat_rate    = rate;
    // g_repeat_delay   = delay;
    g_repeat_active = true;

    wl_event_source_timer_update(g_repeat_timer, delay);
}

void sparrow_text_input_stop_repeat(uint32_t keycode)
{
    if ((keycode != 0) && (g_repeat_keycode != 0) && (keycode != g_repeat_keycode))
    {
        return;
    }

    g_repeat_active  = false;
    g_repeat_keycode = 0;
    g_repeat_sym     = XKB_KEY_NoSymbol;
    g_repeat_unicode = 0;

    if (g_repeat_timer)
    {
        wl_event_source_timer_update(g_repeat_timer, 0);
    }
}

bool sparrow_text_input_is_active()
{
    if (!g_text_input.active)
    {
        return false;
    }

    Core *instance = Core::instance();
    if (instance && instance->seat && (instance->seat->keyboard_state.focused_surface != nullptr))
    {
        return false;
    }

    return true;
}

void sparrow_text_input_init()
{
    Core *instance = Core::instance();

    g_text_input.active = false;
    g_text_input.connection_id = 0;
    g_text_input.text.clear();
    g_text_input.selection_base   = 0;
    g_text_input.selection_extent = 0;
    g_text_input.composing_base   = -1;
    g_text_input.composing_extent = -1;
    g_text_input.multiline    = false;
    g_text_input.input_action = "TextInputAction.done";

    if (instance && instance->wl_event_loop && !g_repeat_timer)
    {
        g_repeat_timer = wl_event_loop_add_timer(instance->wl_event_loop,
            repeat_timer_callback, nullptr);
    }

    g_text_input_channel = std::make_unique<flutter::MethodChannel<rapidjson::Document>>(
        &instance->messenger, "flutter/textinput",
        &flutter::JsonMethodCodec::GetInstance());

    g_text_input_channel->SetMethodCallHandler(
        [] (const flutter::MethodCall<rapidjson::Document>& call,
            std::unique_ptr<flutter::MethodResult<rapidjson::Document>> result)
    {
        const std::string& method = call.method_name();
        const rapidjson::Document *args = call.arguments();

        if (method == "TextInput.setClient")
        {
            if (args && args->IsArray() && (args->Size() >= 2))
            {
                const auto& arr     = args->GetArray();
                g_text_input.active = true;
                g_text_input.connection_id = arr[0].GetInt64();

                g_text_input.text.clear();
                g_text_input.selection_base   = 0;
                g_text_input.selection_extent = 0;
                g_text_input.composing_base   = -1;
                g_text_input.composing_extent = -1;
                g_text_input.multiline    = false;
                g_text_input.input_action = "TextInputAction.done";

                if (arr[1].IsObject())
                {
                    const auto& config = arr[1].GetObject();
                    if (config.HasMember("inputAction") && config["inputAction"].IsString())
                    {
                        g_text_input.input_action = config["inputAction"].GetString();
                    }

                    if (config.HasMember("inputType") && config["inputType"].IsObject())
                    {
                        const auto& input_type = config["inputType"].GetObject();
                        if (input_type.HasMember("name") && input_type["name"].IsString())
                        {
                            if (strstr(input_type["name"].GetString(), "multiline"))
                            {
                                g_text_input.multiline = true;
                            }
                        }
                    }
                }

                sparrow_text_input_stop_repeat(0);

                Core *inst = Core::instance();
                if (inst && inst->seat)
                {
                    wlr_seat_keyboard_clear_focus(inst->seat);
                }

                wlr_log(WLR_INFO, "TextInput.setClient: connection_id=%ld (action=%s, multiline=%d)",
                    g_text_input.connection_id, g_text_input.input_action.c_str(), g_text_input.multiline);
            }

            result->Success();
        } else if (method == "TextInput.setEditingState")
        {
            if (args && args->IsObject())
            {
                const auto& state = args->GetObject();
                if (state.HasMember("text") && state["text"].IsString())
                {
                    g_text_input.text = state["text"].GetString();
                }

                if (state.HasMember("selectionBase") && state["selectionBase"].IsInt())
                {
                    g_text_input.selection_base = state["selectionBase"].GetInt();
                }

                if (state.HasMember("selectionExtent") && state["selectionExtent"].IsInt())
                {
                    g_text_input.selection_extent = state["selectionExtent"].GetInt();
                }

                int32_t text_len = static_cast<int32_t>(g_text_input.text.length());
                if ((g_text_input.selection_base < 0) || (g_text_input.selection_base > text_len))
                {
                    g_text_input.selection_base = text_len;
                }

                if ((g_text_input.selection_extent < 0) || (g_text_input.selection_extent > text_len))
                {
                    g_text_input.selection_extent = g_text_input.selection_base;
                }

                if (state.HasMember("composingBase") && state["composingBase"].IsInt())
                {
                    g_text_input.composing_base = state["composingBase"].GetInt();
                }

                if (state.HasMember("composingExtent") && state["composingExtent"].IsInt())
                {
                    g_text_input.composing_extent = state["composingExtent"].GetInt();
                }
            }

            wlr_log(WLR_DEBUG, "TextInput.setEditingState: text='%s' sel=%d-%d",
                g_text_input.text.c_str(), g_text_input.selection_base,
                g_text_input.selection_extent);
            result->Success();
        } else if (method == "TextInput.clearClient")
        {
            sparrow_text_input_stop_repeat(0);
            g_text_input.active = false;
            g_text_input.connection_id = 0;
            g_text_input.text.clear();
            wlr_log(WLR_INFO, "TextInput.clearClient");
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
            if (method == "TextInput.hide")
            {
                sparrow_text_input_stop_repeat(0);
            }

            result->Success();
        } else
        {
            wlr_log(WLR_INFO, "Unhandled TextInput method: %s", method.c_str());
            result->NotImplemented();
        }
    });

    wlr_log(WLR_INFO,
        "Text input plugin initialized (modern flutter::MethodChannel<rapidjson::Document>)");
}

void sparrow_text_input_handle_key(
    xkb_keysym_t keysym,
    uint32_t unicode,
    bool pressed,
    bool ctrl_active,
    bool shift_active)
{
    if (!g_text_input.active || !pressed)
    {
        return;
    }

    int32_t text_len = static_cast<int32_t>(g_text_input.text.length());
    if ((g_text_input.selection_base < 0) || (g_text_input.selection_base > text_len))
    {
        g_text_input.selection_base = text_len;
    }

    if ((g_text_input.selection_extent < 0) || (g_text_input.selection_extent > text_len))
    {
        g_text_input.selection_extent = g_text_input.selection_base;
    }

    if (ctrl_active)
    {
        if ((keysym == XKB_KEY_c) || (keysym == XKB_KEY_C))
        {
            if (g_text_input.selection_base != g_text_input.selection_extent)
            {
                int32_t start = std::min(g_text_input.selection_base, g_text_input.selection_extent);
                int32_t end   = std::max(g_text_input.selection_base, g_text_input.selection_extent);
                g_clipboard_text = g_text_input.text.substr(start, end - start);
            }

            return;
        }

        if ((keysym == XKB_KEY_x) || (keysym == XKB_KEY_X))
        {
            if (g_text_input.selection_base != g_text_input.selection_extent)
            {
                int32_t start = std::min(g_text_input.selection_base, g_text_input.selection_extent);
                int32_t end   = std::max(g_text_input.selection_base, g_text_input.selection_extent);
                g_clipboard_text = g_text_input.text.substr(start, end - start);
                g_text_input.delete_selection();
                send_editing_state();
            }

            return;
        }

        if ((keysym == XKB_KEY_v) || (keysym == XKB_KEY_V))
        {
            if (!g_clipboard_text.empty())
            {
                g_text_input.delete_selection();
                g_text_input.text.insert(g_text_input.selection_base, g_clipboard_text);
                g_text_input.selection_base  += static_cast<int32_t>(g_clipboard_text.length());
                g_text_input.selection_extent = g_text_input.selection_base;
                send_editing_state();
            }

            return;
        }

        if ((keysym == XKB_KEY_a) || (keysym == XKB_KEY_A))
        {
            g_text_input.selection_base   = 0;
            g_text_input.selection_extent = static_cast<int32_t>(g_text_input.text.length());
            send_editing_state();
            return;
        }
    }

    bool changed = false;

    switch (keysym)
    {
      case XKB_KEY_BackSpace:
        if (g_text_input.selection_base != g_text_input.selection_extent)
        {
            g_text_input.delete_selection();
            changed = true;
        } else if (g_text_input.selection_base > 0)
        {
            int32_t pos = g_text_input.selection_base - 1;
            while ((pos > 0) &&
                   ((static_cast<unsigned char>(g_text_input.text[pos]) & 0xC0) == 0x80))
            {
                pos--;
            }

            g_text_input.text.erase(pos, g_text_input.selection_base - pos);
            g_text_input.selection_base   = pos;
            g_text_input.selection_extent = pos;
            changed = true;
        }

        break;

      case XKB_KEY_Delete:
        if (g_text_input.selection_base != g_text_input.selection_extent)
        {
            g_text_input.delete_selection();
            changed = true;
        } else if (g_text_input.selection_base < static_cast<int32_t>(g_text_input.text.length()))
        {
            int32_t pos = g_text_input.selection_base + 1;
            while ((pos < static_cast<int32_t>(g_text_input.text.length())) &&
                   ((static_cast<unsigned char>(g_text_input.text[pos]) & 0xC0) == 0x80))
            {
                pos++;
            }

            g_text_input.text.erase(g_text_input.selection_base,
                pos - g_text_input.selection_base);
            changed = true;
        }

        break;

      case XKB_KEY_Left:
        if (g_text_input.selection_extent > 0)
        {
            int32_t pos = g_text_input.selection_extent - 1;
            while ((pos > 0) &&
                   ((static_cast<unsigned char>(g_text_input.text[pos]) & 0xC0) == 0x80))
            {
                pos--;
            }

            g_text_input.selection_extent = pos;
            if (!shift_active)
            {
                g_text_input.selection_base = pos;
            }

            changed = true;
        }

        break;

      case XKB_KEY_Right:
        if (g_text_input.selection_extent < static_cast<int32_t>(g_text_input.text.length()))
        {
            int32_t pos = g_text_input.selection_extent + 1;
            while ((pos < static_cast<int32_t>(g_text_input.text.length())) &&
                   ((static_cast<unsigned char>(g_text_input.text[pos]) & 0xC0) == 0x80))
            {
                pos++;
            }

            g_text_input.selection_extent = pos;
            if (!shift_active)
            {
                g_text_input.selection_base = pos;
            }

            changed = true;
        }

        break;

      case XKB_KEY_Home:
        g_text_input.selection_extent = 0;
        if (!shift_active)
        {
            g_text_input.selection_base = 0;
        }

        changed = true;
        break;

      case XKB_KEY_End:
        g_text_input.selection_extent = static_cast<int32_t>(g_text_input.text.length());
        if (!shift_active)
        {
            g_text_input.selection_base = g_text_input.selection_extent;
        }

        changed = true;
        break;

      case XKB_KEY_Return:
      case XKB_KEY_KP_Enter:
        if (g_text_input.multiline)
        {
            g_text_input.delete_selection();
            g_text_input.text.insert(g_text_input.selection_base, "\n");
            g_text_input.selection_base++;
            g_text_input.selection_extent = g_text_input.selection_base;
            changed = true;
        } else
        {
            perform_action(!g_text_input.input_action.empty() ?
                g_text_input.input_action.c_str() :
                "TextInputAction.done");
        }

        break;

      case XKB_KEY_Tab:
        break;

      default:
        if ((unicode >= 0x20) && (unicode != 0x7F))
        {
            char utf8[8] = {0};
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

            g_text_input.delete_selection();
            g_text_input.text.insert(g_text_input.selection_base, utf8, len);
            g_text_input.selection_base  += len;
            g_text_input.selection_extent = g_text_input.selection_base;
            changed = true;
        }

        break;
    }

    if (changed)
    {
        send_editing_state();
    }
}
