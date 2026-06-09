#pragma once

#include <cstdint>
#include <optional>
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

struct ClonePrefabOptions {
  std::optional<uint64_t> new_prefab_id;
  std::optional<uint64_t> prefab_id_start_after;
  double preview_x_step = 1.240311;
  double preview_z_step = 2.238042;
};

struct ClonePrefabSummary {
  uint64_t source_prefab_id = 0;
  std::string source_name;
  uint64_t new_prefab_id = 0;
  std::string new_prefab_name;
  std::optional<uint64_t> target_tab_id;
  std::string target_tab_name;
  size_t cloned_decoration_count = 0;
  double preview_x = 0.0;
  double preview_z = 0.0;
  std::vector<uint32_t> changed_top_fields;
};

struct PrefabCloneMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  ClonePrefabSummary summary;
};

PrefabRenameMutation rename_prefab(const GilFile& file, uint64_t prefab_id, const std::string& new_name);
std::string rename_prefab_summary_to_json(const RenamePrefabSummary& summary);

PrefabCloneMutation clone_prefab_into_tab(
    const GilFile& file,
    uint64_t source_prefab_id,
    const std::string& target_tab_name,
    const std::string& new_prefab_name,
    const ClonePrefabOptions& options = {});

PrefabCloneMutation clone_prefab_into_tab_by_id(
    const GilFile& file,
    uint64_t source_prefab_id,
    uint64_t target_tab_id,
    const std::string& new_prefab_name,
    const ClonePrefabOptions& options = {});

std::string clone_prefab_summary_to_json(const ClonePrefabSummary& summary);

}  // namespace opengil
