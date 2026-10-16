// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "json_message_codec.h"

#include <iostream>
#include <string>
#include <vector>

#include "fast_json_serializer.h"
#include <simdjson.h>

namespace flutter
{

namespace
{

EncodableValue SimdjsonToEncodableValue(const simdjson::dom::element& elem)
{
    switch (elem.type())
    {
        case simdjson::dom::element_type::NULL_VALUE:
            return EncodableValue();
        case simdjson::dom::element_type::BOOL:
            return EncodableValue(elem.get_bool().value());
        case simdjson::dom::element_type::INT64:
            return EncodableValue(elem.get_int64().value());
        case simdjson::dom::element_type::UINT64:
            return EncodableValue(static_cast<int64_t>(elem.get_uint64().value()));
        case simdjson::dom::element_type::DOUBLE:
            return EncodableValue(elem.get_double().value());
        case simdjson::dom::element_type::STRING:
            return EncodableValue(std::string(elem.get_string().value()));
        case simdjson::dom::element_type::ARRAY:
        {
            EncodableList list;
            for (auto child : elem.get_array())
            {
                list.push_back(SimdjsonToEncodableValue(child));
            }
            return EncodableValue(std::move(list));
        }
        case simdjson::dom::element_type::OBJECT:
        {
            EncodableMap map;
            for (auto field : elem.get_object())
            {
                map.emplace(EncodableValue(std::string(field.key)),
                            SimdjsonToEncodableValue(field.value));
            }
            return EncodableValue(std::move(map));
        }
        default:
            return EncodableValue();
    }
}

} // namespace

// static
const JsonMessageCodec& JsonMessageCodec::GetInstance()
{
    static JsonMessageCodec sInstance;
    return sInstance;
}

std::unique_ptr<std::vector<uint8_t>> JsonMessageCodec::EncodeMessageInternal(
    const EncodableValue& message) const
{
    std::string json = FastJsonSerializer::Serialize(message);
    return std::make_unique<std::vector<uint8_t>>(json.begin(), json.end());
}

std::unique_ptr<EncodableValue> JsonMessageCodec::DecodeMessageInternal(
    const uint8_t *binary_message,
    const size_t message_size) const
{
    if (!binary_message || message_size == 0)
    {
        return nullptr;
    }

    thread_local simdjson::dom::parser parser;
    auto doc_res = parser.parse_unpadded(
        reinterpret_cast<const char*>(binary_message), message_size);
    if (doc_res.error())
    {
        std::cerr << "Unable to parse JSON message with simdjson: "
                  << simdjson::error_message(doc_res.error()) << '\n';
        return nullptr;
    }

    return std::make_unique<EncodableValue>(SimdjsonToEncodableValue(doc_res.value()));
}

} // namespace flutter
