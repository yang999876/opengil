#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

inline constexpr uint64_t kUiAssetImageRectResourceId = 100001;
inline constexpr uint64_t kUiAssetImageCircleResourceId = 100002;
inline constexpr uint64_t kUiAssetImageTriangleResourceId = 100003;
inline constexpr uint64_t kDefaultUiAssetsControllerEntryId = 1073741841;

inline constexpr uint64_t kUiPrimitiveRectangle = kUiAssetImageRectResourceId;
inline constexpr uint64_t kUiPrimitiveEllipse = kUiAssetImageCircleResourceId;
inline constexpr uint64_t kUiPrimitiveTriangle = kUiAssetImageTriangleResourceId;
inline constexpr uint64_t kDefaultUiPrimitiveControllerEntryId = 1073741840;

struct UiVec2 {
  std::optional<double> x;
  std::optional<double> y;
};

struct UiVec3 {
  std::optional<double> x;
  std::optional<double> y;
  std::optional<double> z;
};

struct UiAssetTransform {
  UiVec2 position;
  UiVec2 size;
  UiVec3 scale;
  std::optional<double> rotation_z;
};

struct UiAsset {
  size_t asset_index = 0;
  size_t primitive_index = 0;
  size_t top9_index = 0;
  std::optional<uint64_t> entry_id;
  std::optional<uint64_t> parent_entry_id;
  std::optional<uint64_t> controller_entry_id;
  std::string kind;
  std::optional<std::string> name;
  std::optional<uint64_t> resource_id;
  std::optional<uint64_t> primitive_type_id;
  std::optional<int64_t> color;
  std::optional<uint64_t> raw_color;
  std::optional<uint64_t> layer;
  UiAssetTransform transform;
  std::vector<uint64_t> child_entry_ids;
  UiVec2 mask_size;
};

struct UiAssetList {
  uint64_t parent_entry_id = kDefaultUiAssetsControllerEntryId;
  bool has_top9 = false;
  bool has_top46 = false;
  std::vector<UiAsset> assets;
};

UiAssetList list_ui_assets(
    const GilFile& file,
    uint64_t parent_entry_id = kDefaultUiAssetsControllerEntryId);

using UiPrimitiveTransform = UiAssetTransform;
using UiPrimitive = UiAsset;

struct UiPrimitiveList {
  uint64_t controller_entry_id = kDefaultUiPrimitiveControllerEntryId;
  bool has_top9 = false;
  bool has_top46 = false;
  std::vector<UiPrimitive> primitives;
};

UiPrimitiveList list_ui_primitives(
    const GilFile& file,
    uint64_t controller_entry_id = kDefaultUiPrimitiveControllerEntryId);

}  // namespace opengil
