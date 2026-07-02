#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/ui_structure_ops.hpp"

namespace opengil {

struct UiAssetImageSpec {
  uint64_t resource_id = kUiAssetImageRectResourceId;
  uint64_t primitive_type_id = kUiAssetImageRectResourceId;
  double x = 0.0;
  double y = 0.0;
  double width = 1.0;
  double height = 1.0;
  double scale_x = 1.0;
  double scale_y = 1.0;
  double scale_z = 1.0;
  double rotation_z = 0.0;
  int64_t color = -1;
  uint64_t layer = 9;
  std::string name;
};

struct UiAssetCreateOptions {
  uint64_t parent_entry_id = kDefaultUiAssetsControllerEntryId;
  uint64_t target_controller_entry_id = kDefaultUiAssetsControllerEntryId;
};

UiStructureMutation create_ui_asset_images(
    const GilFile& file,
    std::span<const UiAssetImageSpec> images,
    const UiAssetCreateOptions& options = {});

struct UiAssetGroupSpec {
  std::string name = "Asset Group";
  double x = 0.0;
  double y = 0.0;
  double width = 1.0;
  double height = 1.0;
  double scale_x = 1.0;
  double scale_y = 1.0;
  double scale_z = 1.0;
  double rotation_z = 0.0;
  std::optional<double> mask_width;
  std::optional<double> mask_height;
};

UiStructureMutation create_ui_asset_group(
    const GilFile& file,
    const UiAssetGroupSpec& group,
    const std::vector<size_t>& child_asset_indexes,
    const UiAssetCreateOptions& options = {});

using UiGeneratedPrimitiveSpec = UiAssetImageSpec;
using UiGeneratedPrimitiveOptions = UiAssetCreateOptions;

UiStructureMutation append_generated_ui_primitives(
    const GilFile& file,
    std::span<const UiGeneratedPrimitiveSpec> primitives,
    const UiGeneratedPrimitiveOptions& options = {});

}  // namespace opengil
