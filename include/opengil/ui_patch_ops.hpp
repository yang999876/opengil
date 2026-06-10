#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"

namespace opengil {

struct UiPrimitivePatchSummary {
  std::string kind;
  size_t primitive_index = 0;
  std::optional<uint64_t> entry_id;
  UiPrimitive before;
  UiPrimitive after;
  std::vector<uint32_t> changed_top_fields;
};

struct UiPrimitivePatchMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  UiPrimitivePatchSummary summary;
};

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

std::string ui_primitive_patch_summary_to_json(const UiPrimitivePatchSummary& summary);

}  // namespace opengil
