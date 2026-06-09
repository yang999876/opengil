#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/object_ops.hpp"

namespace opengil {

struct DecorationSpec {
  uint64_t asset_id = 0;
  std::string name;
  Transform transform;
};

struct DecorationSummary {
  uint64_t prefab_id = 0;
  size_t scene_instance_count = 0;
  std::vector<uint64_t> prefab_decoration_ids;
  std::vector<uint64_t> scene_decoration_ids;
  std::vector<uint32_t> changed_top_fields;
};

struct DecorationMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  DecorationSummary summary;
};

DecorationMutation add_prefab_decorations(
    const GilFile& file,
    uint64_t prefab_id,
    const std::vector<DecorationSpec>& specs);

std::string decoration_summary_to_json(const DecorationSummary& summary);

}  // namespace opengil
