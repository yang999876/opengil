#include "opengil/pixel_decoration_import_ops.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "opengil/decoration_ops.hpp"
#include "opengil/model_ops.hpp"

#include "png_loader.hpp"

namespace opengil {
namespace {

inline constexpr size_t kMaxPixels = 10000;

GilFile file_from_bytes(const GilFile& base, const std::vector<uint8_t>& bytes) {
  GilFile file;
  file.path = base.path;
  file.header = base.header;
  file.bytes = bytes;
  return file;
}

std::vector<DecorationSpec> decoration_specs_from_image(
    const PngRgbaImage& image,
    uint64_t asset_id,
    double pixel_size) {
  const size_t width = static_cast<size_t>(image.width);
  const size_t height = static_cast<size_t>(image.height);
  const size_t pixel_count = width * height;
  if (pixel_count > kMaxPixels) throw std::runtime_error("PNG exceeds 10000 pixel import limit");

  std::vector<DecorationSpec> specs;
  specs.reserve(pixel_count);
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t offset = (y * width + x) * 4;
      if (image.pixels[offset + 3] == 0) continue;

      DecorationSpec spec;
      spec.asset_id = asset_id;
      spec.name = "pixel_" + std::to_string(x) + "_" + std::to_string(y);
      spec.transform.position.x = static_cast<double>(x) * pixel_size;
      spec.transform.position.y = 0.0;
      spec.transform.position.z = static_cast<double>(y) * pixel_size;
      spec.transform.scale.x = pixel_size;
      spec.transform.scale.y = pixel_size;
      spec.transform.scale.z = pixel_size;
      specs.push_back(std::move(spec));
    }
  }
  return specs;
}

void merge_changed_top_fields(std::vector<uint32_t>& out, const std::vector<uint32_t>& values) {
  for (uint32_t value : values) {
    if (std::find(out.begin(), out.end(), value) == out.end()) out.push_back(value);
  }
}

}  // namespace

PixelDecorationImportMutation import_pixel_png_as_decoration_prefab(
    const GilFile& file,
    const std::filesystem::path& png_path,
    const PixelDecorationImportOptions& options) {
  if (options.prefab_id == 0) throw std::runtime_error("prefab id is required");
  if (options.asset_id == 0) throw std::runtime_error("placeholder decoration asset id is required");
  if (options.pixel_size <= 0.0) throw std::runtime_error("pixel-size must be positive");

  const auto image = load_png_rgba_file(png_path, "pixel decoration import");
  auto decoration_specs = decoration_specs_from_image(image, options.asset_id, options.pixel_size);

  CreatePrefabOptions prefab_options;
  prefab_options.prefab_id = options.prefab_id;
  const auto created = create_prefab(file, options.asset_id, prefab_options);
  auto current = file_from_bytes(file, created.bytes);

  const auto emptied = set_prefab_to_empty_model(current, options.prefab_id);
  current = file_from_bytes(current, emptied.bytes);

  PixelDecorationImportSummary summary;
  summary.prefab_id = options.prefab_id;
  summary.asset_id = options.asset_id;
  summary.source_pixel_count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  summary.decoration_count = decoration_specs.size();
  merge_changed_top_fields(summary.changed_top_fields, created.changed_top_fields);
  merge_changed_top_fields(summary.changed_top_fields, emptied.model_summary.changed_top_fields);

  if (!decoration_specs.empty()) {
    const auto decorated = add_prefab_decorations(current, options.prefab_id, decoration_specs);
    current = file_from_bytes(current, decorated.bytes);
    summary.prefab_decoration_ids = decorated.summary.prefab_decoration_ids;
    merge_changed_top_fields(summary.changed_top_fields, decorated.summary.changed_top_fields);
  }

  PixelDecorationImportMutation mutation;
  mutation.bytes = std::move(current.bytes);
  mutation.payload = std::vector<uint8_t>(payload(current).begin(), payload(current).end());
  mutation.summary = std::move(summary);
  return mutation;
}

}  // namespace opengil
