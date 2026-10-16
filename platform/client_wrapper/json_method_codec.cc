// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "json_method_codec.h"

#include "json_message_codec.h"

namespace flutter
{

namespace
{

// Keys used in MethodCall encoding.
constexpr char kMessageMethodKey[]    = "method";
constexpr char kMessageArgumentsKey[] = "args";

} // namespace

// static
const JsonMethodCodec& JsonMethodCodec::GetInstance()
{
    static JsonMethodCodec sInstance;
    return sInstance;
}

std::unique_ptr<MethodCall<EncodableValue>>
JsonMethodCodec::DecodeMethodCallInternal(const uint8_t *message,
    size_t message_size) const
{
    std::unique_ptr<EncodableValue> json_message =
        JsonMessageCodec::GetInstance().DecodeMessage(message, message_size);
    if (!json_message || !std::holds_alternative<EncodableMap>(*json_message))
    {
        return nullptr;
    }

    const auto& map = std::get<EncodableMap>(*json_message);
    auto method_it = map.find(EncodableValue(kMessageMethodKey));
    if (method_it == map.end() || !std::holds_alternative<std::string>(method_it->second))
    {
        return nullptr;
    }

    std::string method_name = std::get<std::string>(method_it->second);
    auto args_it = map.find(EncodableValue(kMessageArgumentsKey));
    std::unique_ptr<EncodableValue> arguments;
    if (args_it != map.end())
    {
        arguments = std::make_unique<EncodableValue>(args_it->second);
    }

    return std::make_unique<MethodCall<EncodableValue>>(
        method_name, std::move(arguments));
}

std::unique_ptr<std::vector<uint8_t>> JsonMethodCodec::EncodeMethodCallInternal(
    const MethodCall<EncodableValue>& method_call) const
{
    EncodableMap message;
    message[EncodableValue(kMessageMethodKey)] =
        EncodableValue(method_call.method_name());

    if (method_call.arguments())
    {
        message[EncodableValue(kMessageArgumentsKey)] = *method_call.arguments();
    }

    return JsonMessageCodec::GetInstance().EncodeMessage(EncodableValue(std::move(message)));
}

std::unique_ptr<std::vector<uint8_t>>
JsonMethodCodec::EncodeSuccessEnvelopeInternal(
    const EncodableValue *result) const
{
    EncodableList envelope;
    envelope.push_back(result ? *result : EncodableValue());
    return JsonMessageCodec::GetInstance().EncodeMessage(EncodableValue(std::move(envelope)));
}

std::unique_ptr<std::vector<uint8_t>>
JsonMethodCodec::EncodeErrorEnvelopeInternal(
    const std::string& error_code,
    const std::string& error_message,
    const EncodableValue *error_details) const
{
    EncodableList envelope;
    envelope.push_back(EncodableValue(error_code));
    envelope.push_back(EncodableValue(error_message));
    envelope.push_back(error_details ? *error_details : EncodableValue());
    return JsonMessageCodec::GetInstance().EncodeMessage(EncodableValue(std::move(envelope)));
}

bool JsonMethodCodec::DecodeAndProcessResponseEnvelopeInternal(
    const uint8_t *response,
    size_t response_size,
    MethodResult<EncodableValue> *result) const
{
    std::unique_ptr<EncodableValue> json_response =
        JsonMessageCodec::GetInstance().DecodeMessage(response, response_size);
    if (!json_response || !std::holds_alternative<EncodableList>(*json_response))
    {
        return false;
    }

    const auto& list = std::get<EncodableList>(*json_response);
    switch (list.size())
    {
        case 1:
        {
            if (list[0].IsNull())
            {
                result->Success();
            } else
            {
                result->Success(list[0]);
            }
            return true;
        }
        case 3:
        {
            std::string code = std::holds_alternative<std::string>(list[0]) ?
                std::get<std::string>(list[0]) : "";
            std::string message = std::holds_alternative<std::string>(list[1]) ?
                std::get<std::string>(list[1]) : "";
            if (list[2].IsNull())
            {
                result->Error(code, message);
            } else
            {
                result->Error(code, message, list[2]);
            }
            return true;
        }
        default:
            return false;
    }
}

} // namespace flutter
