#pragma once

#include <cstdint>
#include <optional>
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

struct CreateSceneObjectOptions {
  std::optional<uint64_t> object_id;
  Transform transform;
};

struct CreatePrefabOptions {
  std::optional<uint64_t> prefab_id;
  Transform transform;
};

struct CreateScenePrefabInstanceOptions {
  std::optional<uint64_t> object_id;
  Transform transform;
};

ObjectMutation create_scene_object(const GilFile& file, uint64_t asset_id, const CreateSceneObjectOptions& options);
ObjectMutation create_prefab(
    const GilFile& file,
    uint64_t asset_id,
    const CreatePrefabOptions& options,
    const GilFile* template_file = nullptr);
ObjectMutation create_scene_prefab_instance(
    const GilFile& file,
    uint64_t prefab_id,
    uint64_t asset_id,
    const CreateScenePrefabInstanceOptions& options,
    const GilFile* template_file = nullptr);

ObjectMutation set_scene_transform(const GilFile& file, uint64_t object_id, const Transform& transform);
ObjectMutation set_preview_transform(const GilFile& file, uint64_t object_id, const Transform& transform);

std::string transform_summary_to_json(const TransformSummary& summary);

}  // namespace opengil
