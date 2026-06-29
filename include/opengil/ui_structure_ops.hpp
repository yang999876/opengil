#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"

namespace opengil {

struct UiStructureSummary {
  std::string kind;
  uint64_t target_controller_entry_id = kDefaultUiPrimitiveControllerEntryId;
  std::vector<uint64_t> entry_ids;
  size_t primitive_count = 0;
  std::vector<uint32_t> changed_top_fields;
};

struct UiStructureMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  UiStructureSummary summary;
};

struct UiDeleteOptions {
  std::optional<uint64_t> target_controller_entry_id;
};

UiStructureMutation delete_ui_primitives(
    const GilFile& file,
    const std::vector<size_t>& primitive_indexes,
    const UiDeleteOptions& options = {});

}  // namespace opengil
