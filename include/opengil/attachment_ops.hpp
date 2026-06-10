#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

struct AttachmentPointSpec {
  std::string name;
  std::string display_name;
  double x = 0.0;
  double y = 0.0;
  double rot_x = 0.0;
  double rot_y = 0.0;
};

struct AttachmentSummary {
  uint64_t prefab_id = 0;
  std::optional<uint64_t> object_id;
  size_t scene_instance_count = 0;
  std::vector<std::string> names;
  std::vector<uint32_t> changed_top_fields;
};

struct AttachmentMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  AttachmentSummary summary;
};

AttachmentMutation add_attachment_points(
    const GilFile& file,
    uint64_t prefab_id,
    std::optional<uint64_t> object_id,
    const std::vector<AttachmentPointSpec>& specs);

}  // namespace opengil
