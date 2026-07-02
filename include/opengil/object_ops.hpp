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

struct ObjectSummary {
  std::string kind;
  std::optional<uint64_t> object_id;
  std::optional<uint64_t> prefab_id;
  std::optional<uint64_t> asset_id;
  Transform transform;
  std::vector<uint32_t> changed_top_fields;
};

struct ObjectMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  ObjectSummary summary;
  std::vector<uint32_t> changed_top_fields;
};

struct ObjectColorSummary {
  std::string kind;
  uint64_t object_id = 0;
  std::optional<int64_t> before_color;
  std::optional<uint64_t> before_raw_color;
  std::optional<uint64_t> before_rgb_color;
  std::optional<bool> before_enabled;
  int64_t after_color = 0;
  uint64_t after_raw_color = 0;
  uint64_t after_rgb_color = 0;
  bool after_enabled = true;
  std::vector<uint32_t> changed_top_fields;
};

struct ObjectColorMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  ObjectColorSummary summary;
  std::vector<uint32_t> changed_top_fields;
};

struct CreateSceneObjectOptions {
  std::optional<uint64_t> object_id;
  Transform transform;
};

struct CreatePrefabOptions {
  std::optional<uint64_t> prefab_id;
  std::optional<std::string> name;
  Transform transform;
};

struct CreateScenePrefabInstanceOptions {
  std::optional<uint64_t> object_id;
  Transform transform;
};

struct CreatePrefabPreviewOptions {
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
ObjectMutation create_prefab_preview(
    const GilFile& file,
    uint64_t prefab_id,
    const CreatePrefabPreviewOptions& options);

ObjectMutation set_scene_transform(const GilFile& file, uint64_t object_id, const Transform& transform);
ObjectMutation set_preview_transform(const GilFile& file, uint64_t object_id, const Transform& transform);
ObjectMutation set_scene_object_asset_id(const GilFile& file, uint64_t object_id, uint64_t asset_id);
ObjectColorMutation set_scene_object_color(const GilFile& file, uint64_t object_id, int64_t color);

}  // namespace opengil
