#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

struct RenamePrefabSummary {
  uint64_t prefab_id = 0;
  std::string before_name;
  std::string after_name;
  std::vector<uint32_t> changed_top_fields;
};

struct PrefabRenameMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  RenamePrefabSummary summary;
};

PrefabRenameMutation rename_prefab(const GilFile& file, uint64_t prefab_id, const std::string& new_name);
std::string rename_prefab_summary_to_json(const RenamePrefabSummary& summary);

}  // namespace opengil

