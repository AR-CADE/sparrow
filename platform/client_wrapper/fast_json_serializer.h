#ifndef FAST_JSON_SERIALIZER_H_
#define FAST_JSON_SERIALIZER_H_

#include "encodable_value.h"

#include <cstdio>
#include <string>
#include <variant>

namespace flutter {

class FastJsonSerializer {
 public:
  static std::string Serialize(const EncodableValue& val) {
    std::string out;
    out.reserve(128);
    SerializeValue(val, out);
    return out;
  }

  static void SerializeValue(const EncodableValue& val, std::string& out) {
    if (val.IsNull()) {
      out.append("null");
    } else if (std::holds_alternative<bool>(val)) {
      out.append(std::get<bool>(val) ? "true" : "false");
    } else if (std::holds_alternative<int32_t>(val)) {
      out.append(std::to_string(std::get<int32_t>(val)));
    } else if (std::holds_alternative<int64_t>(val)) {
      out.append(std::to_string(std::get<int64_t>(val)));
    } else if (std::holds_alternative<double>(val)) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%.17g", std::get<double>(val));
      out.append(buf);
    } else if (std::holds_alternative<std::string>(val)) {
      SerializeString(std::get<std::string>(val), out);
    } else if (std::holds_alternative<EncodableList>(val)) {
      out.push_back('[');
      const auto& list = std::get<EncodableList>(val);
      for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) out.push_back(',');
        SerializeValue(list[i], out);
      }
      out.push_back(']');
    } else if (std::holds_alternative<EncodableMap>(val)) {
      out.push_back('{');
      const auto& map = std::get<EncodableMap>(val);
      bool first = true;
      for (const auto& [k, v] : map) {
        if (!first) out.push_back(',');
        first = false;
        if (std::holds_alternative<std::string>(k)) {
          SerializeString(std::get<std::string>(k), out);
        } else if (std::holds_alternative<int32_t>(k) || std::holds_alternative<int64_t>(k)) {
          SerializeString(std::to_string(k.LongValue()), out);
        } else {
          SerializeString(Serialize(k), out);
        }
        out.push_back(':');
        SerializeValue(v, out);
      }
      out.push_back('}');
    } else {
      out.append("null");
    }
  }

  static void SerializeString(const std::string& s, std::string& out) {
    out.push_back('"');
    for (char c : s) {
      switch (c) {
        case '"': out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\b': out.append("\\b"); break;
        case '\f': out.append("\\f"); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        default:
          if (static_cast<unsigned char>(c) < 0x20) {
            char hex[8];
            snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned char>(c));
            out.append(hex);
          } else {
            out.push_back(c);
          }
          break;
      }
    }
    out.push_back('"');
  }
};

}  // namespace flutter

#endif  // FAST_JSON_SERIALIZER_H_
