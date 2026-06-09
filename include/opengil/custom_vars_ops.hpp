#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

struct CustomVariableInfo {
  std::string name;
  uint64_t type_id = 0;
  std::string type;
  std::optional<uint64_t> enabled;
};

struct PrefabCustomVariables {
  uint64_t prefab_id = 0;
  std::string prefab_name;
  std::vector<CustomVariableInfo> variables;
};

struct CustomVarsMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  std::string result_json;
  std::vector<uint32_t> changed_top_fields;
};

std::vector<PrefabCustomVariables> list_prefab_custom_variables(
    const GilFile& file,
    std::optional<uint64_t> prefab_id = std::nullopt);

CustomVarsMutation add_prefab_custom_variable(
    const GilFile& file,
    uint64_t prefab_id,
    const std::string& name,
    const std::string& type);

CustomVarsMutation remove_prefab_custom_variable(
    const GilFile& file,
    uint64_t prefab_id,
    const std::string& name);

CustomVarsMutation copy_prefab_custom_variables(
    const GilFile& file,
    uint64_t source_prefab_id,
    uint64_t target_prefab_id);

CustomVarsMutation sync_tab_custom_variables(
    const GilFile& file,
    uint64_t source_prefab_id,
    const std::string& tab_name);

CustomVarsMutation sync_tab_custom_variables_by_tab_id(
    const GilFile& file,
    uint64_t source_prefab_id,
    uint64_t tab_id);

std::string custom_variables_list_to_json(const std::vector<PrefabCustomVariables>& rows);

}  // namespace opengil
