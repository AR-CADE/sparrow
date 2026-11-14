#ifndef BINARY_MESSENGER_HPP
#define BINARY_MESSENGER_HPP

#include "flutter_embedder.h"
#include <cstdint>
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_set>

class IncomingMessageDispatcher;

namespace flutter
{
using BinaryReply = std::function<void (const uint8_t*reply, size_t reply_size)>;

using BinaryMessageHandler = std::function<void (const uint8_t*message, size_t message_size,
    BinaryReply reply)>;

class BinaryMessenger
{
  public:
    virtual ~BinaryMessenger();

    virtual void Send(const std::string& channel,
        const uint8_t *message,
        size_t message_size,
        BinaryReply reply = nullptr) const;

    virtual void SetMessageHandler(const std::string& channel,
        BinaryMessageHandler handler);

    void SetMessageDispatcher(IncomingMessageDispatcher *message_dispatcher);

    FlutterEngine GetEngine();

    void SetEngine(FlutterEngine engine, const FlutterEngineProcTable *api);

    void Shutdown();

    const FlutterEngineProcTable * GetApi() const
    {
        return api_;
    }

  private:
    struct PendingReply;

    FlutterEngine engine_ = nullptr;
    const FlutterEngineProcTable *api_ = nullptr;

    IncomingMessageDispatcher *message_dispatcher = nullptr;

    std::map<std::string, BinaryMessageHandler> handlers_;

    mutable std::mutex pending_replies_mutex_;
    mutable std::unordered_set<PendingReply*> pending_replies_;
};
} // namespace flutter

using flutter::BinaryReply;
using flutter::BinaryMessageHandler;
using flutter::BinaryMessenger;

#endif
