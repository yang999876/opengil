#include "test_support.hpp"
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

  OPENGIL_CHECK(value.is_object());
  const auto* ops = value.find("ops");
  OPENGIL_CHECK(ops);
  OPENGIL_CHECK(ops->is_array());
  OPENGIL_CHECK(ops->array_value.size() == 2);
  const auto* first = ops->array_value[0].find("prefabId");
  OPENGIL_CHECK(first);
  OPENGIL_CHECK(first->is_unsigned());
  OPENGIL_CHECK(first->unsigned_value == 1086324737);
  const auto* second_name = ops->array_value[1].find("name");
  OPENGIL_CHECK(second_name);
  OPENGIL_CHECK(second_name->is_string());
  OPENGIL_CHECK(second_name->string_value == "openGil");
  const auto* number = ops->array_value[1].find("x");
  OPENGIL_CHECK(number);
  OPENGIL_CHECK(number->is_number());
  OPENGIL_CHECK(number->number_value == -3.5);
  const auto* dry_run = value.find("dryRun");
  OPENGIL_CHECK(dry_run);
  OPENGIL_CHECK(dry_run->is_bool());
  OPENGIL_CHECK(dry_run->bool_value);

  return 0;
}
