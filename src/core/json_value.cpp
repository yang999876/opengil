#include "opengil/json_value.hpp"

#include <cctype>
#include <limits>
#include <stdexcept>

namespace opengil::json {
namespace {

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  Value parse() {
    skip_ws();
    Value value = parse_value_inner();
    skip_ws();
    if (pos_ != text_.size()) {
      fail("trailing content after JSON value");
    }
    return value;
  }

 private:
  std::string_view text_;
  size_t pos_ = 0;

  [[noreturn]] void fail(const char* message) const {
    throw std::runtime_error(std::string("JSON parse error: ") + message);
  }

  char peek() const {
    return pos_ < text_.size() ? text_[pos_] : '\0';
  }

  char take() {
    if (pos_ >= text_.size()) fail("unexpected end of input");
    return text_[pos_++];
  }

  void expect(char ch) {
    if (take() != ch) fail("unexpected character");
  }

  void skip_ws() {
    while (pos_ < text_.size()) {
      const unsigned char ch = static_cast<unsigned char>(text_[pos_]);
      if (!std::isspace(ch)) break;
      pos_++;
    }
  }

  bool consume_literal(std::string_view literal) {
    if (text_.substr(pos_, literal.size()) != literal) return false;
    pos_ += literal.size();
    return true;
  }

  Value parse_value_inner() {
    skip_ws();
    const char ch = peek();
    if (ch == '{') return parse_object();
    if (ch == '[') return parse_array();
    if (ch == '"') return parse_string_value();
    if (ch >= '0' && ch <= '9') return parse_unsigned();
    if (consume_literal("true")) {
      Value value;
      value.type = Value::Type::Bool;
      value.bool_value = true;
      return value;
    }
    if (consume_literal("false")) {
      Value value;
      value.type = Value::Type::Bool;
      value.bool_value = false;
      return value;
    }
    if (consume_literal("null")) {
      return Value{};
    }
    fail("expected JSON value");
  }

  static void append_utf8(std::string& out, uint32_t codepoint) {
    if (codepoint <= 0x7f) {
      out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
      out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  uint32_t parse_hex4() {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = take();
      value <<= 4;
      if (ch >= '0' && ch <= '9') {
        value |= static_cast<uint32_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        value |= static_cast<uint32_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        value |= static_cast<uint32_t>(ch - 'A' + 10);
      } else {
        fail("invalid unicode escape");
      }
    }
    return value;
  }

  std::string parse_string_raw() {
    expect('"');
    std::string out;
    while (true) {
      const char ch = take();
      if (ch == '"') return out;
      if (static_cast<unsigned char>(ch) < 0x20) fail("control character in string");
      if (ch != '\\') {
        out.push_back(ch);
        continue;
      }

      const char esc = take();
      switch (esc) {
        case '"':
        case '\\':
        case '/':
          out.push_back(esc);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u':
          append_utf8(out, parse_hex4());
          break;
        default:
          fail("invalid escape");
      }
    }
  }

  Value parse_string_value() {
    Value value;
    value.type = Value::Type::String;
    value.string_value = parse_string_raw();
    return value;
  }

  Value parse_unsigned() {
    Value value;
    value.type = Value::Type::Unsigned;
    uint64_t number = 0;
    if (peek() == '0') {
      pos_++;
      if (peek() >= '0' && peek() <= '9') fail("leading zero in number");
    } else {
      while (peek() >= '0' && peek() <= '9') {
        const uint64_t digit = static_cast<uint64_t>(take() - '0');
        if (number > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
          fail("unsigned integer overflow");
        }
        number = number * 10 + digit;
      }
    }
    if (peek() == '.' || peek() == 'e' || peek() == 'E') {
      fail("only unsigned integer JSON numbers are supported");
    }
    value.unsigned_value = number;
    return value;
  }

  Value parse_array() {
    Value value;
    value.type = Value::Type::Array;
    expect('[');
    skip_ws();
    if (peek() == ']') {
      pos_++;
      return value;
    }
    while (true) {
      value.array_value.push_back(parse_value_inner());
      skip_ws();
      const char ch = take();
      if (ch == ']') return value;
      if (ch != ',') fail("expected comma in array");
    }
  }

  Value parse_object() {
    Value value;
    value.type = Value::Type::Object;
    expect('{');
    skip_ws();
    if (peek() == '}') {
      pos_++;
      return value;
    }
    while (true) {
      skip_ws();
      if (peek() != '"') fail("expected object key");
      const std::string key = parse_string_raw();
      skip_ws();
      expect(':');
      value.object_value[key] = parse_value_inner();
      skip_ws();
      const char ch = take();
      if (ch == '}') return value;
      if (ch != ',') fail("expected comma in object");
    }
  }
};

}  // namespace

const Value* Value::find(std::string_view key) const {
  if (!is_object()) return nullptr;
  const auto it = object_value.find(std::string(key));
  return it == object_value.end() ? nullptr : &it->second;
}

Value parse_value(std::string_view text) {
  return Parser(text).parse();
}

}  // namespace opengil::json
