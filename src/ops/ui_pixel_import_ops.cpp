#include "opengil/ui_pixel_import_ops.hpp"

#include <algorithm>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "opengil/ui_generated_ops.hpp"

#include "png_loader.hpp"

namespace opengil {
namespace {

int64_t argb_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  const uint32_t raw = (static_cast<uint32_t>(a) << 24) |
                       (static_cast<uint32_t>(r) << 16) |
                       (static_cast<uint32_t>(g) << 8) |
                       static_cast<uint32_t>(b);
  return static_cast<int64_t>(static_cast<int32_t>(raw));
}

struct UiAssetImageBounds {
  double min_x = std::numeric_limits<double>::max();
  double min_y = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double max_y = std::numeric_limits<double>::lowest();
};

UiAssetImageBounds bounds_for_images(std::span<const UiAssetImageSpec> specs) {
  UiAssetImageBounds bounds;
  for (const auto& spec : specs) {
    const double half_width = spec.width * 0.5;
    const double half_height = spec.height * 0.5;
    bounds.min_x = std::min(bounds.min_x, spec.x - half_width);
    bounds.min_y = std::min(bounds.min_y, spec.y - half_height);
    bounds.max_x = std::max(bounds.max_x, spec.x + half_width);
    bounds.max_y = std::max(bounds.max_y, spec.y + half_height);
  }
  return bounds;
}

std::vector<UiAssetImageSpec> specs_from_image(const PngRgbaImage& image, double pixel_size) {
  const size_t width = static_cast<size_t>(image.width);
  const size_t height = static_cast<size_t>(image.height);
  const size_t pixel_count = width * height;
  if (pixel_count > kUiPixelImportMaxPixels) throw std::runtime_error("PNG exceeds 10000 pixel import limit");

  std::vector<UiAssetImageSpec> specs;
  specs.reserve(pixel_count);
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t offset = (y * width + x) * 4;
      if (image.pixels[offset + 3] == 0) continue;

      UiAssetImageSpec spec;
      spec.resource_id = kUiAssetImageRectResourceId;
      spec.x = static_cast<double>(x) * pixel_size;
      spec.y = static_cast<double>(height - 1 - y) * pixel_size;
      spec.width = pixel_size;
      spec.height = pixel_size;
      spec.color = argb_color(
          image.pixels[offset],
          image.pixels[offset + 1],
          image.pixels[offset + 2],
          image.pixels[offset + 3]);
      spec.layer = 9;
      spec.name = "pixel_" + std::to_string(x) + "_" + std::to_string(y);
      specs.push_back(std::move(spec));
    }
  }
  return specs;
}

GilFile file_from_mutation(const GilFile& source, const UiStructureMutation& mutation) {
  GilFile file;
  file.path = source.path;
  file.header = source.header;
  file.bytes = mutation.bytes;
  return file;
}

std::string group_name_from_png_path(const std::filesystem::path& png_path) {
  const auto stem = png_path.stem().string();
  return stem.empty() ? "Pixel Group" : stem;
}

}  // namespace

UiStructureMutation import_pixel_png_as_ui_asset_images(
    const GilFile& file,
    const std::filesystem::path& png_path,
    const UiPixelImportOptions& options) {
  if (options.pixel_size <= 0.0) throw std::runtime_error("pixel-size must be positive");
  const uint64_t parent_entry_id = options.target_parent_entry_id != kDefaultUiAssetsControllerEntryId
      ? options.target_parent_entry_id
      : options.target_controller_entry_id;

  const auto image = load_png_rgba_file(png_path, "pixel import");
  auto specs = specs_from_image(image, options.pixel_size);
  if (specs.empty()) {
    const auto list = list_ui_assets(file, parent_entry_id);
    UiStructureMutation mutation;
    mutation.payload = std::vector<uint8_t>(payload(file).begin(), payload(file).end());
    mutation.bytes = file.bytes;
    mutation.summary.kind = "importPixelPngUiAssetImages";
    mutation.summary.target_controller_entry_id = parent_entry_id;
    mutation.summary.primitive_count = list.assets.size();
    mutation.summary.entry_ids.reserve(mutation.summary.primitive_count);
    for (const auto& asset : list.assets) {
      if (asset.entry_id) mutation.summary.entry_ids.push_back(*asset.entry_id);
    }
    return mutation;
  }

  UiAssetCreateOptions generated_options;
  generated_options.parent_entry_id = parent_entry_id;
  const auto before_assets = list_ui_assets(file, parent_entry_id);
  std::unordered_set<uint64_t> before_entry_ids;
  for (const auto& asset : before_assets.assets) {
    if (asset.entry_id) before_entry_ids.insert(*asset.entry_id);
  }
  auto image_mutation = create_ui_asset_images(file, specs, generated_options);
  auto image_file = file_from_mutation(file, image_mutation);

  std::vector<size_t> child_asset_indexes;
  const auto after_assets = list_ui_assets(image_file, parent_entry_id);
  for (size_t i = 0; i < after_assets.assets.size(); ++i) {
    const auto& asset = after_assets.assets[i];
    if (asset.kind == "image" && asset.entry_id && !before_entry_ids.contains(*asset.entry_id)) {
      child_asset_indexes.push_back(i);
    }
  }
  if (child_asset_indexes.size() != specs.size()) {
    throw std::runtime_error("imported UI asset image indexes not found");
  }

  UiAssetGroupSpec group;
  group.name = group_name_from_png_path(png_path);
  const auto bounds = bounds_for_images(specs);
  group.width = bounds.max_x - bounds.min_x;
  group.height = bounds.max_y - bounds.min_y;
  group.x = (bounds.min_x + bounds.max_x) * 0.5;
  group.y = (bounds.min_y + bounds.max_y) * 0.5;
  group.mask_width = group.width;
  group.mask_height = group.height;

  auto group_mutation = create_ui_asset_group(image_file, group, child_asset_indexes, generated_options);
  group_mutation.summary.kind = "importPixelPngUiAssetGroup";
  return group_mutation;
}

UiStructureMutation import_pixel_png_as_ui_primitives(
    const GilFile& file,
    const std::filesystem::path& png_path,
    const UiPixelImportOptions& options) {
  return import_pixel_png_as_ui_asset_images(file, png_path, options);
}

}  // namespace opengil
