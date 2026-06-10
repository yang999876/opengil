#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

struct ProjectileMotionInput {
  float x = 0.0f;
  float y = 0.0f;
  std::optional<float> gravity;
};

struct ProjectileMotionSummary {
  uint64_t prefab_id = 0;
  std::string prefab_name;
  std::optional<float> before_x;
  std::optional<float> before_y;
  std::optional<float> before_gravity;
  float after_x = 0.0f;
  float after_y = 0.0f;
  std::optional<float> after_gravity;
  std::vector<uint32_t> changed_top_fields;
};

struct ProjectileMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  ProjectileMotionSummary summary;
};

ProjectileMotionInput projectile_motion_from_angle(float angle_deg, float speed, std::optional<float> gravity);
ProjectileMutation set_prefab_projectile_motion(
    const GilFile& file,
    uint64_t prefab_id,
    const ProjectileMotionInput& motion);

}  // namespace opengil
