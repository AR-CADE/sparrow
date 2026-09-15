#include "keyboard_channel.hpp"
#include "client_wrapper/encodable_value.h"
#include "client_wrapper/method_channel.h"
#include "client_wrapper/standard_method_codec.h"
#include "core.hpp"
#include <sparrow/nonstd/wlroots-full.hpp>

static std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> g_keyboard_channel;

void sparrow_keyboard_channel_init()
{
    Core *instance = Core::instance();
    if (!instance)
    {
        return;
    }

    g_keyboard_channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        &instance->messenger, "flutter/keyboard",
        &flutter::StandardMethodCodec::GetInstance());

    g_keyboard_channel->SetMethodCallHandler(
        [] (const flutter::MethodCall<flutter::EncodableValue> & call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
    {
        const std::string & method = call.method_name();
        if (method == "getKeyboardState")
        {
            flutter::EncodableMap map;
            result->Success(flutter::EncodableValue(map));
        } else
        {
            wlr_log(WLR_INFO, "Unhandled flutter/keyboard method: %s", method.c_str());
            result->NotImplemented();
        }
    });

    wlr_log(WLR_INFO, "Keyboard channel plugin initialized (flutter/keyboard)");
}

void sparrow_keyboard_channel_destroy()
{
    g_keyboard_channel.reset();
}
