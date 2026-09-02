#include "incoming_message_dispatcher.hpp"

IncomingMessageDispatcher::IncomingMessageDispatcher(
    FlutterDesktopMessengerRef messenger) :
    messenger_(messenger)
{}

IncomingMessageDispatcher::~IncomingMessageDispatcher() = default;

bool IncomingMessageDispatcher::HandleMessage(
    const FlutterDesktopMessage& message,
    const std::function<void(void)>& input_block_cb,
    const std::function<void(void)>& input_unblock_cb)
{
    std::string channel(message.channel);

    // Find the handler for the channel; if there isn't one, return false
    // so the caller can try other dispatch mechanisms (legacy fallback).
    if (callbacks_.find(channel) == callbacks_.end())
    {
        return false;
    }

    auto& callback_info = callbacks_[channel];
    FlutterDesktopMessageCallback message_callback = callback_info.first;

    // Process the call, handling input blocking if requested.
    bool block_input = input_blocking_channels_.count(channel) > 0;
    if (block_input)
    {
        input_block_cb();
    }

    message_callback(messenger_, &message, callback_info.second);
    if (block_input)
    {
        input_unblock_cb();
    }

    return true;
}

void IncomingMessageDispatcher::SetMessageCallback(
    const std::string& channel,
    FlutterDesktopMessageCallback callback,
    void *user_data)
{
    if (!callback)
    {
        callbacks_.erase(channel);
        return;
    }

    callbacks_[channel] = std::make_pair(callback, user_data);
}

void IncomingMessageDispatcher::EnableInputBlockingForChannel(
    const std::string& channel)
{
    input_blocking_channels_.insert(channel);
}
