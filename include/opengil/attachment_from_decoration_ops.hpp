#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "opengil/attachment_ops.hpp"
#include "opengil/gil.hpp"

namespace opengil {

AttachmentMutation add_attachment_points_from_decorations(
    const GilFile& file,
    uint64_t prefab_id,
    std::optional<uint64_t> object_id = std::nullopt);

std::string attachment_from_decoration_summary_to_json(const AttachmentSummary& summary);

}  // namespace opengil
