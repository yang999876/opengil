#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"

namespace opengil {

struct UiAssetPatchSummary {
  std::string kind;
  size_t asset_index = 0;
  size_t primitive_index = 0;
  std::optional<uint64_t> entry_id;
  UiAsset before;
  UiAsset after;
  std::vector<uint32_t> changed_top_fields;
};

struct UiAssetPatchMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  UiAssetPatchSummary summary;
};

UiAssetPatchMutation set_ui_asset_image_resource(
    const GilFile& file,
    size_t asset_index,
    uint64_t resource_id,
    uint64_t parent_entry_id = kDefaultUiAssetsControllerEntryId);

UiAssetPatchMutation set_ui_asset_image_color(
    const GilFile& file,
    size_t asset_index,
    int64_t color,
    uint64_t parent_entry_id = kDefaultUiAssetsControllerEntryId);

UiAssetPatchMutation set_ui_asset_image_transform(
    const GilFile& file,
    size_t asset_index,
    const UiAssetTransform& transform,
    uint64_t parent_entry_id = kDefaultUiAssetsControllerEntryId);

UiAssetPatchMutation set_ui_asset_image_layer(
    const GilFile& file,
    size_t asset_index,
    uint64_t layer,
    uint64_t parent_entry_id = kDefaultUiAssetsControllerEntryId);

UiAssetPatchMutation set_ui_asset_image_name(
    const GilFile& file,
    size_t asset_index,
    const std::string& name,
    uint64_t parent_entry_id = kDefaultUiAssetsControllerEntryId);

using UiPrimitivePatchSummary = UiAssetPatchSummary;
using UiPrimitivePatchMutation = UiAssetPatchMutation;

UiPrimitivePatchMutation set_ui_primitive_type(
    const GilFile& file,
    size_t primitive_index,
    uint64_t primitive_type_id,
    uint64_t controller_entry_id = kDefaultUiPrimitiveControllerEntryId);

UiPrimitivePatchMutation set_ui_primitive_color(
    const GilFile& file,
    size_t primitive_index,
    int64_t color,
    uint64_t controller_entry_id = kDefaultUiPrimitiveControllerEntryId);

UiPrimitivePatchMutation set_ui_primitive_transform(
    const GilFile& file,
    size_t primitive_index,
    const UiPrimitiveTransform& transform,
    uint64_t controller_entry_id = kDefaultUiPrimitiveControllerEntryId);

UiPrimitivePatchMutation set_ui_primitive_layer(
    const GilFile& file,
    size_t primitive_index,
    uint64_t layer,
    uint64_t controller_entry_id = kDefaultUiPrimitiveControllerEntryId);

UiPrimitivePatchMutation set_ui_primitive_name(
    const GilFile& file,
    size_t primitive_index,
    const std::string& name,
    uint64_t controller_entry_id = kDefaultUiPrimitiveControllerEntryId);

}  // namespace opengil
