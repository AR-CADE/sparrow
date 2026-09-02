#include <iostream>
#include "binary_messenger.hpp"
#include "incoming_message_dispatcher.hpp"

using FlutterDesktopMessengerRef = BinaryMessenger*;

static bool send_message_with_reply(FlutterEngine engine, const FlutterEngineProcTable *api,
    const char *channel, const uint8_t *message,
    const size_t message_size,
    const FlutterDataCallback reply, void *user_data)
{
    FlutterPlatformMessageResponseHandle *response_handle = nullptr;
    if ((reply != nullptr) && (user_data != nullptr))
    {
        FlutterEngineResult result =
            api->PlatformMessageCreateResponseHandle(engine, reply, user_data, &response_handle);
        if (result != kSuccess)
        {
            std::cerr << "ERROR: Failed to create response handle\n";
            return false;
        }
    }

    FlutterPlatformMessage platform_message = {
        sizeof(FlutterPlatformMessage),
        channel,
        message,
        message_size,
        response_handle,
    };

    FlutterEngineResult message_result = api->SendPlatformMessage(engine, &platform_message);
    if (response_handle != nullptr)
    {
        api->PlatformMessageReleaseResponseHandle(engine, response_handle);
    }

    return message_result == kSuccess;
}

void ForwardToHandler(FlutterDesktopMessengerRef messenger,
    const FlutterPlatformMessage *message,
    void *user_data)
{
    auto *response_handle = message->response_handle;
    const FlutterEngineProcTable *api = messenger->GetApi();
    FlutterEngine engine = messenger->GetEngine();
    BinaryReply reply_handler = [messenger, response_handle] (
        const uint8_t *reply,
        size_t reply_size) mutable
    {
        if (!response_handle)
        {
            return;
        }

        const FlutterEngineProcTable *api = messenger ? messenger->GetApi() : nullptr;
        FlutterEngine engine = messenger ? messenger->GetEngine() : nullptr;

        if ((api != nullptr) && (engine != nullptr) && (api->SendPlatformMessageResponse != nullptr))
        {
            api->SendPlatformMessageResponse(engine, response_handle, reply,
                reply_size);
        }

        response_handle = nullptr;
    };

    const BinaryMessageHandler& message_handler =
        *static_cast<BinaryMessageHandler*>(user_data);

    message_handler(message->message, message->message_size,
        std::move(reply_handler));
}

void BinaryMessenger::Send(const std::string& channel,
    const uint8_t *message,
    size_t message_size,
    BinaryReply reply) const
{
    if (reply == nullptr)
    {
        send_message_with_reply(engine_, api_, channel.c_str(), message, message_size, nullptr, nullptr);
        return;
    }

    struct Captures
    {
        BinaryReply reply;
    };

    auto captures = new Captures();
    captures->reply = reply;

    auto message_reply = [] (const uint8_t *data, size_t data_size,
                             void *user_data)
    {
        auto captures = reinterpret_cast<Captures*>(user_data);
        captures->reply(data, data_size);
        delete captures;
    };
    bool result = send_message_with_reply(engine_, api_, channel.c_str(), message, message_size,
        message_reply, captures);
    if (!result)
    {
        delete captures;
    }
}

void BinaryMessenger::SetMessageHandler(const std::string& channel,
    BinaryMessageHandler handler)
{
    if (!handler)
    {
        handlers_.erase(channel);
        message_dispatcher->SetMessageCallback(channel, nullptr, nullptr);
        return;
    }

    handlers_[channel] = std::move(handler);
    BinaryMessageHandler *message_handler = &handlers_[channel];
    message_dispatcher->SetMessageCallback(channel, ForwardToHandler, message_handler);
}

void BinaryMessenger::SetMessageDispatcher(IncomingMessageDispatcher *message_dispatcher)
{
    this->message_dispatcher = message_dispatcher;
}

FlutterEngine BinaryMessenger::GetEngine()
{
    return engine_;
}

void BinaryMessenger::SetEngine(FlutterEngine engine, const FlutterEngineProcTable *api)
{
    engine_ = engine;
    api_    = api;
}
