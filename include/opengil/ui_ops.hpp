#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

inline constexpr uint64_t kUiPrimitiveRectangle = 100001;
inline constexpr uint64_t kUiPrimitiveEllipse = 100002;
inline constexpr uint64_t kUiPrimitiveTriangle = 100003;
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

struct UiPrimitiveTransform {
  UiVec2 position;
  UiVec2 size;
  UiVec3 scale;
  std::optional<double> rotation_z;
};

struct UiPrimitive {
  size_t primitive_index = 0;
  size_t top9_index = 0;
  std::optional<uint64_t> entry_id;
  std::optional<uint64_t> controller_entry_id;
  std::optional<std::string> name;
  std::optional<uint64_t> primitive_type_id;
  std::optional<int64_t> color;
  std::optional<uint64_t> raw_color;
  std::optional<uint64_t> layer;
  UiPrimitiveTransform transform;
};

struct UiPrimitiveList {
  uint64_t controller_entry_id = kDefaultUiPrimitiveControllerEntryId;
  bool has_top9 = false;
  bool has_top46 = false;
  std::vector<UiPrimitive> primitives;
};

UiPrimitiveList list_ui_primitives(
    const GilFile& file,
    uint64_t controller_entry_id = kDefaultUiPrimitiveControllerEntryId);

std::string ui_primitive_list_to_json(const UiPrimitiveList& list);

}  // namespace opengil
