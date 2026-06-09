#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace opengil::json {

inline std::string escape(std::string_view value) {
  std::ostringstream out;
  for (unsigned char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(ch) << std::dec << std::setfill(' ');
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  return out.str();
}

inline std::string quote(std::string_view value) {
  return "\"" + escape(value) + "\"";
}

inline std::string bool_value(bool value) {
  return value ? "true" : "false";
}

inline std::string null_or_quote(const std::string& value, bool has_value) {
  return has_value ? quote(value) : "null";
}

template <typename T>
inline std::string number(T value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

inline std::string array_of_numbers(const std::vector<uint32_t>& values) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out << ",";
    out << values[i];
  }
  out << "]";
  return out.str();
}

inline std::string string_array(const std::vector<std::string>& values) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out << ",";
    out << quote(values[i]);
  }
  out << "]";
  return out.str();
}

}  // namespace opengil::json

