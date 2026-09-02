#include "text_input.hpp"
#include "client_wrapper/json_method_codec.h"
#include "client_wrapper/method_channel.h"
#include "core.hpp"

#include <rapidjson/document.h>
#include <xkbcommon/xkbcommon.h>

static struct text_input_state g_text_input = {};
static std::unique_ptr<flutter::MethodChannel<rapidjson::Document>> g_text_input_channel;

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
    state.AddMember("text", rapidjson::Value(g_text_input.text, allocator), allocator);
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

void sparrow_text_input_init()
{
    Core *instance = Core::instance();

    memset(&g_text_input, 0, sizeof(g_text_input));
    g_text_input.composing_base   = -1;
    g_text_input.composing_extent = -1;

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

                memset(g_text_input.text, 0, sizeof(g_text_input.text));
                g_text_input.text_length    = 0;
                g_text_input.selection_base = 0;
                g_text_input.selection_extent = 0;
                g_text_input.composing_base   = -1;
                g_text_input.composing_extent = -1;
                g_text_input.multiline = false;
                memset(g_text_input.input_action, 0, sizeof(g_text_input.input_action));

                if (arr[1].IsObject())
                {
                    const auto& config = arr[1].GetObject();
                    if (config.HasMember("inputAction") && config["inputAction"].IsString())
                    {
                        strncpy(g_text_input.input_action, config["inputAction"].GetString(),
                            sizeof(g_text_input.input_action) - 1);
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

                wlr_log(WLR_INFO, "TextInput.setClient: connection_id=%ld",
                    g_text_input.connection_id);
            }

            result->Success();
        } else if (method == "TextInput.setEditingState")
        {
            if (args && args->IsObject())
            {
                const auto& state = args->GetObject();
                if (state.HasMember("text") && state["text"].IsString())
                {
                    strncpy(g_text_input.text, state["text"].GetString(),
                        sizeof(g_text_input.text) - 1);
                    g_text_input.text_length = strlen(g_text_input.text);
                }

                if (state.HasMember("selectionBase") && state["selectionBase"].IsNumber())
                {
                    g_text_input.selection_base = state["selectionBase"].GetInt();
                }

                if (state.HasMember("selectionExtent") && state["selectionExtent"].IsNumber())
                {
                    g_text_input.selection_extent = state["selectionExtent"].GetInt();
                }

                if (state.HasMember("composingBase") && state["composingBase"].IsNumber())
                {
                    g_text_input.composing_base = state["composingBase"].GetInt();
                }

                if (state.HasMember("composingExtent") && state["composingExtent"].IsNumber())
                {
                    g_text_input.composing_extent = state["composingExtent"].GetInt();
                }
            }

            wlr_log(WLR_DEBUG, "TextInput.setEditingState: text='%s' sel=%d-%d",
                g_text_input.text, g_text_input.selection_base,
                g_text_input.selection_extent);
            result->Success();
        } else if (method == "TextInput.clearClient")
        {
            g_text_input.active = false;
            g_text_input.connection_id = 0;
            memset(g_text_input.text, 0, sizeof(g_text_input.text));
            g_text_input.text_length = 0;
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

static void delete_selection(void)
{
    if (g_text_input.selection_base == g_text_input.selection_extent)
    {
        return;
    }

    const int32_t start =
        g_text_input.selection_base < g_text_input.selection_extent ?
        g_text_input.selection_base :
        g_text_input.selection_extent;
    const int32_t end =
        g_text_input.selection_base > g_text_input.selection_extent ?
        g_text_input.selection_base :
        g_text_input.selection_extent;

    memmove(&g_text_input.text[start], &g_text_input.text[end],
        g_text_input.text_length - end + 1);
    g_text_input.text_length   -= (end - start);
    g_text_input.selection_base = start;
    g_text_input.selection_extent = start;
}

static void insert_text(const char *str, size_t len)
{
    if (g_text_input.text_length + len >= TEXT_INPUT_MAX_LENGTH)
    {
        return;
    }

    delete_selection();

    const int32_t pos = g_text_input.selection_base;
    memmove(&g_text_input.text[pos + len], &g_text_input.text[pos],
        g_text_input.text_length - pos + 1);
    memcpy(&g_text_input.text[pos], str, len);
    g_text_input.text_length   += len;
    g_text_input.selection_base = pos + len;
    g_text_input.selection_extent = pos + len;
}

void sparrow_text_input_handle_key(xkb_keysym_t keysym,
    uint32_t unicode, bool pressed)
{
    if (!g_text_input.active || !pressed)
    {
        return;
    }

    bool changed = false;

    switch (keysym)
    {
      case XKB_KEY_BackSpace:
        if (g_text_input.selection_base != g_text_input.selection_extent)
        {
            delete_selection();
            changed = true;
        } else if (g_text_input.selection_base > 0)
        {
            const int32_t pos = g_text_input.selection_base - 1;
            memmove(&g_text_input.text[pos], &g_text_input.text[pos + 1],
                g_text_input.text_length - pos);
            g_text_input.text_length--;
            g_text_input.selection_base   = pos;
            g_text_input.selection_extent = pos;
            changed = true;
        }

        break;

      case XKB_KEY_Delete:
        if (g_text_input.selection_base != g_text_input.selection_extent)
        {
            delete_selection();
            changed = true;
        } else if (g_text_input.selection_base <
                   (int32_t)g_text_input.text_length)
        {
            const int32_t pos = g_text_input.selection_base;
            memmove(&g_text_input.text[pos], &g_text_input.text[pos + 1],
                g_text_input.text_length - pos);
            g_text_input.text_length--;
            changed = true;
        }

        break;

      case XKB_KEY_Left:
        if (g_text_input.selection_base > 0)
        {
            g_text_input.selection_base--;
            g_text_input.selection_extent = g_text_input.selection_base;
            changed = true;
        }

        break;

      case XKB_KEY_Right:
        if (g_text_input.selection_base < (int32_t)g_text_input.text_length)
        {
            g_text_input.selection_base++;
            g_text_input.selection_extent = g_text_input.selection_base;
            changed = true;
        }

        break;

      case XKB_KEY_Home:
        g_text_input.selection_base   = 0;
        g_text_input.selection_extent = 0;
        changed = true;
        break;

      case XKB_KEY_End:
        g_text_input.selection_base   = g_text_input.text_length;
        g_text_input.selection_extent = g_text_input.text_length;
        changed = true;
        break;

      case XKB_KEY_Return:
      case XKB_KEY_KP_Enter:
        if (g_text_input.multiline)
        {
            insert_text("\n", 1);
            changed = true;
        } else
        {
            perform_action(g_text_input.input_action[0] ?
                g_text_input.input_action :
                "TextInputAction.done");
        }

        break;

      case XKB_KEY_Tab:
        break;

      default:
        if ((unicode >= 0x20) && (unicode != 0x7F))
        {
            char utf8[8];
            int len = 0;
            if (unicode < 0x80)
            {
                utf8[0] = unicode;
                len     = 1;
            } else if (unicode < 0x800)
            {
                utf8[0] = 0xC0 | (unicode >> 6);
                utf8[1] = 0x80 | (unicode & 0x3F);
                len     = 2;
            } else if (unicode < 0x10000)
            {
                utf8[0] = 0xE0 | (unicode >> 12);
                utf8[1] = 0x80 | ((unicode >> 6) & 0x3F);
                utf8[2] = 0x80 | (unicode & 0x3F);
                len     = 3;
            } else
            {
                utf8[0] = 0xF0 | (unicode >> 18);
                utf8[1] = 0x80 | ((unicode >> 12) & 0x3F);
                utf8[2] = 0x80 | ((unicode >> 6) & 0x3F);
                utf8[3] = 0x80 | (unicode & 0x3F);
                len     = 4;
            }

            insert_text(utf8, len);
            changed = true;
        }

        break;
    }

    if (changed)
    {
        send_editing_state();
    }
}
