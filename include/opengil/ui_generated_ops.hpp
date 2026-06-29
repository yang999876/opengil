#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/ui_structure_ops.hpp"

namespace opengil {

struct UiGeneratedPrimitiveSpec {
  uint64_t primitive_type_id = kUiPrimitiveRectangle;
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

struct UiGeneratedPrimitiveOptions {
  uint64_t target_controller_entry_id = kDefaultUiPrimitiveControllerEntryId;
};

UiStructureMutation append_generated_ui_primitives(
    const GilFile& file,
    std::span<const UiGeneratedPrimitiveSpec> primitives,
    const UiGeneratedPrimitiveOptions& options = {});

}  // namespace opengil
