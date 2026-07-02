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

struct DeletePrefabSummary {
  uint64_t prefab_id = 0;
  std::vector<uint64_t> removed_decoration_ids;
  std::vector<uint32_t> changed_top_fields;
};

struct PrefabDeleteMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  DeletePrefabSummary summary;
};

struct PrefabTabSummary {
  std::string kind;
  std::optional<uint64_t> tab_id;
  std::string tab_name;
  uint64_t prefab_id = 0;
  std::optional<uint64_t> source_tab_id;
  std::string source_tab_name;
  std::optional<uint64_t> target_tab_id;
  std::string target_tab_name;
  std::vector<uint32_t> changed_top_fields;
};

struct PrefabTabMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  PrefabTabSummary summary;
};

PrefabRenameMutation rename_prefab(const GilFile& file, uint64_t prefab_id, const std::string& new_name);

PrefabTabMutation create_prefab_tab(
    const GilFile& file,
    const std::string& tab_name,
    const std::optional<uint64_t>& tab_id = std::nullopt);

PrefabTabMutation delete_prefab_tab_by_id(const GilFile& file, uint64_t tab_id);
PrefabTabMutation delete_prefab_tab(const GilFile& file, const std::string& tab_name);

PrefabTabMutation move_prefab_to_tab_by_id(const GilFile& file, uint64_t prefab_id, uint64_t tab_id);
PrefabTabMutation move_prefab_to_tab(const GilFile& file, uint64_t prefab_id, const std::string& tab_name);
PrefabTabMutation move_prefab_to_uncategorized(const GilFile& file, uint64_t prefab_id);

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

PrefabCloneMutation copy_prefab_to_tab(
    const GilFile& file,
    uint64_t source_prefab_id,
    const std::string& target_tab_name,
    const std::optional<std::string>& new_prefab_name = std::nullopt,
    const ClonePrefabOptions& options = {});

PrefabCloneMutation copy_prefab_to_tab_by_id(
    const GilFile& file,
    uint64_t source_prefab_id,
    uint64_t target_tab_id,
    const std::optional<std::string>& new_prefab_name = std::nullopt,
    const ClonePrefabOptions& options = {});

PrefabDeleteMutation delete_prefab(const GilFile& file, uint64_t prefab_id);

}  // namespace opengil
