#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

struct TabInfo {
  std::optional<uint64_t> id;
  std::string name;
  std::vector<uint64_t> prefab_ids;
};

struct PrefabInfo {
  uint64_t prefab_id = 0;
  std::optional<uint64_t> model_asset_id;
  std::string name;
};

struct ModelInfo {
  uint64_t prefab_id = 0;
  std::string name;
  std::optional<uint64_t> prefab_model_asset_id;
  std::vector<uint64_t> scene_model_asset_ids;
  std::vector<uint64_t> preview_model_asset_ids;
};

struct NodeGraphInfo {
  std::string path;
  std::string role;
  std::optional<uint64_t> id;
  std::string name;
  size_t node_count = 0;
  size_t composite_pin_count = 0;
  size_t comment_count = 0;
  size_t graph_value_count = 0;
  size_t affiliation_count = 0;
};

std::vector<TabInfo> list_tabs(const GilFile& file);
std::vector<PrefabInfo> list_prefabs(const GilFile& file, const std::optional<std::string>& tab_name = std::nullopt);
std::vector<TabInfo> list_prefab_tabs(const GilFile& file, uint64_t prefab_id);
std::optional<ModelInfo> get_model_info(const GilFile& file, uint64_t prefab_id);
std::vector<NodeGraphInfo> list_nodegraphs(const GilFile& file);

}  // namespace opengil
