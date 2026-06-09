#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opengil::json {

struct Value {
  enum class Type {
    Null,
    Bool,
    Unsigned,
    String,
    Array,
    Object,
  };

  Type type = Type::Null;
  bool bool_value = false;
  uint64_t unsigned_value = 0;
  std::string string_value;
  std::vector<Value> array_value;
  std::map<std::string, Value> object_value;

  bool is_null() const { return type == Type::Null; }
  bool is_bool() const { return type == Type::Bool; }
  bool is_unsigned() const { return type == Type::Unsigned; }
  bool is_string() const { return type == Type::String; }
  bool is_array() const { return type == Type::Array; }
  bool is_object() const { return type == Type::Object; }

  const Value* find(std::string_view key) const;
};

Value parse_value(std::string_view text);

}  // namespace opengil::json
