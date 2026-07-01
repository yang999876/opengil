#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/object_ops.hpp"

namespace opengil {

struct PixelDecorationImportOptions {
  uint64_t prefab_id = 0;
  uint64_t asset_id = 0;
  double pixel_size = 1.0;
  bool merge_same_color_rects = true;
};

struct PixelDecorationImportSummary {
  uint64_t prefab_id = 0;
  uint64_t asset_id = 0;
  size_t source_pixel_count = 0;
  size_t decoration_count = 0;
  bool merge_same_color_rects = true;
  std::vector<uint64_t> prefab_decoration_ids;
  std::vector<uint32_t> changed_top_fields;
};

struct PixelDecorationImportMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  PixelDecorationImportSummary summary;
};

PixelDecorationImportMutation import_pixel_png_as_decoration_prefab(
    const GilFile& file,
    const std::filesystem::path& png_path,
    const PixelDecorationImportOptions& options);

}  // namespace opengil
