#pragma once

#include <cstdint>
#include <filesystem>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/ui_structure_ops.hpp"

namespace opengil {

inline constexpr size_t kUiPixelImportMaxPixels = 10000;

struct UiPixelImportOptions {
  double pixel_size = 1.0;
  uint64_t target_parent_entry_id = kDefaultUiAssetsControllerEntryId;
  uint64_t target_controller_entry_id = kDefaultUiAssetsControllerEntryId;
};

UiStructureMutation import_pixel_png_as_ui_asset_images(
    const GilFile& file,
    const std::filesystem::path& png_path,
    const UiPixelImportOptions& options = {});

UiStructureMutation import_pixel_png_as_ui_primitives(
    const GilFile& file,
    const std::filesystem::path& png_path,
    const UiPixelImportOptions& options = {});

}  // namespace opengil
