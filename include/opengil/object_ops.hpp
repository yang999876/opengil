#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Transform {
  Vec3 position{0.0, 0.0, 0.0};
  Vec3 rotation{0.0, 0.0, 0.0};
  Vec3 scale{1.0, 1.0, 1.0};
};

struct TransformSummary {
  std::string kind;
  uint64_t object_id = 0;
  Transform transform;
  std::vector<uint32_t> changed_top_fields;
};

struct ObjectMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  std::string result_json;
  std::vector<uint32_t> changed_top_fields;
};

ObjectMutation set_scene_transform(const GilFile& file, uint64_t object_id, const Transform& transform);
ObjectMutation set_preview_transform(const GilFile& file, uint64_t object_id, const Transform& transform);

std::string transform_summary_to_json(const TransformSummary& summary);

}  // namespace opengil
