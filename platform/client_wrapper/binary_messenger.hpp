#pragma once

#include "flutter_embedder.h"
#include <cstdint>
#include <string>
#include <functional>
#include <map>

class IncomingMessageDispatcher;

using BinaryReply = std::function<void (const uint8_t*reply, size_t reply_size)>;

using BinaryMessageHandler = std::function<void (const uint8_t*message, size_t message_size,
    BinaryReply reply)>;

class BinaryMessenger
{
  public:
    void Send(const std::string& channel,
        const uint8_t *message,
        size_t message_size,
        BinaryReply reply = nullptr) const;

    void SetMessageHandler(const std::string& channel,
        BinaryMessageHandler handler);

    void SetMessageDispatcher(IncomingMessageDispatcher *message_dispatcher);

    FlutterEngine GetEngine();

    void SetEngine(FlutterEngine engine, const FlutterEngineProcTable *api);

    const FlutterEngineProcTable * GetApi() const
    {
        return api_;
    }

  private:
    FlutterEngine engine_ = nullptr;
    const FlutterEngineProcTable *api_ = nullptr;

    IncomingMessageDispatcher *message_dispatcher = nullptr;

    std::map<std::string, BinaryMessageHandler> handlers_;
};
