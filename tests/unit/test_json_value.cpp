#include <cassert>
#include <string>

#include "opengil/json_value.hpp"

int main() {
  const auto value = opengil::json::parse_value(R"({
    "ops": [
      {"op": "set-model", "prefabId": 1086324737, "assetId": 20001220},
      {"op": "rename-prefab", "name": "openGil", "x": -3.5}
    ],
    "dryRun": true
  })");

  assert(value.is_object());
  const auto* ops = value.find("ops");
  assert(ops);
  assert(ops->is_array());
  assert(ops->array_value.size() == 2);
  const auto* first = ops->array_value[0].find("prefabId");
  assert(first);
  assert(first->is_unsigned());
  assert(first->unsigned_value == 1086324737);
  const auto* second_name = ops->array_value[1].find("name");
  assert(second_name);
  assert(second_name->is_string());
  assert(second_name->string_value == "openGil");
  const auto* number = ops->array_value[1].find("x");
  assert(number);
  assert(number->is_number());
  assert(number->number_value == -3.5);
  const auto* dry_run = value.find("dryRun");
  assert(dry_run);
  assert(dry_run->is_bool());
  assert(dry_run->bool_value);

  return 0;
}
