#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

constexpr uint64_t EMPTY_MODEL_ASSET_ID = 10005018;
constexpr uint64_t EMPTY_MODEL_TAG_ID = 20;
constexpr uint32_t EMPTY_MODEL_PAYLOAD_FIELD = 29;

struct SetModelSummary {
  uint64_t prefab_id = 0;
  std::string prefab_name;
  uint64_t model_asset_id = 0;
  bool prefab_updated = false;
  size_t scene_updated = 0;
  size_t preview_updated = 0;
  std::vector<uint32_t> changed_top_fields;
};

struct GilMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  SetModelSummary model_summary;
};

GilMutation set_prefab_model_asset_id(const GilFile& file, uint64_t prefab_id, uint64_t model_asset_id);
GilMutation set_prefab_to_empty_model(const GilFile& file, uint64_t prefab_id);

}  // namespace opengil
