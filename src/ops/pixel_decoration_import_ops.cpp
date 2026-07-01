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

struct PixelColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  friend bool operator==(const PixelColor&, const PixelColor&) = default;
};

struct MergedPixelRect {
  size_t x = 0;
  size_t y = 0;
  size_t width = 0;
  size_t height = 0;
  PixelColor color;
};

GilFile file_from_bytes(const GilFile& base, const std::vector<uint8_t>& bytes) {
  GilFile file;
  file.path = base.path;
  file.header = base.header;
  file.bytes = bytes;
  return file;
}

size_t pixel_offset(size_t width, size_t x, size_t y) {
  return (y * width + x) * 4;
}

bool is_visible_pixel(const PngRgbaImage& image, size_t width, size_t x, size_t y) {
  return image.pixels[pixel_offset(width, x, y) + 3] != 0;
}

PixelColor pixel_color_at(const PngRgbaImage& image, size_t width, size_t x, size_t y) {
  const size_t offset = pixel_offset(width, x, y);
  return PixelColor{
      image.pixels[offset],
      image.pixels[offset + 1],
      image.pixels[offset + 2],
  };
}

bool can_merge_pixel(
    const PngRgbaImage& image,
    size_t width,
    size_t x,
    size_t y,
    const PixelColor& color,
    const std::vector<bool>& used) {
  if (used[y * width + x]) return false;
  if (!is_visible_pixel(image, width, x, y)) return false;
  return pixel_color_at(image, width, x, y) == color;
}

bool can_extend_rect_down(
    const PngRgbaImage& image,
    size_t width,
    size_t y,
    size_t x_begin,
    size_t rect_width,
    const PixelColor& color,
    const std::vector<bool>& used) {
  for (size_t x = x_begin; x < x_begin + rect_width; ++x) {
    if (!can_merge_pixel(image, width, x, y, color, used)) return false;
  }
  return true;
}

std::vector<MergedPixelRect> merge_same_color_rects(const PngRgbaImage& image) {
  const size_t width = static_cast<size_t>(image.width);
  const size_t height = static_cast<size_t>(image.height);
  const size_t pixel_count = width * height;
  if (pixel_count > kMaxPixels) throw std::runtime_error("PNG exceeds 10000 pixel import limit");

  std::vector<bool> used(pixel_count, false);
  std::vector<MergedPixelRect> rects;
  rects.reserve(pixel_count);

  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      if (used[y * width + x]) continue;
      if (!is_visible_pixel(image, width, x, y)) continue;

      const PixelColor color = pixel_color_at(image, width, x, y);

      size_t rect_width = 1;
      while (x + rect_width < width &&
             can_merge_pixel(image, width, x + rect_width, y, color, used)) {
        ++rect_width;
      }

      size_t rect_height = 1;
      while (y + rect_height < height &&
             can_extend_rect_down(image, width, y + rect_height, x, rect_width, color, used)) {
        ++rect_height;
      }

      for (size_t used_y = y; used_y < y + rect_height; ++used_y) {
        for (size_t used_x = x; used_x < x + rect_width; ++used_x) {
          used[used_y * width + used_x] = true;
        }
      }

      rects.push_back(MergedPixelRect{x, y, rect_width, rect_height, color});
    }
  }

  return rects;
}

std::vector<DecorationSpec> decoration_specs_from_rects(
    const std::vector<MergedPixelRect>& rects,
    uint64_t asset_id,
    double pixel_size) {
  std::vector<DecorationSpec> specs;
  specs.reserve(rects.size());

  for (const auto& rect : rects) {
    DecorationSpec spec;
    spec.asset_id = asset_id;
    spec.name = "pixel_" + std::to_string(rect.x) + "_" + std::to_string(rect.y);
    if (rect.width != 1 || rect.height != 1) {
      spec.name += "_" + std::to_string(rect.width) + "x" + std::to_string(rect.height);
    }
    spec.transform.position.x =
        (static_cast<double>(rect.x) + static_cast<double>(rect.width - 1) * 0.5) * pixel_size;
    spec.transform.position.y = 0.0;
    spec.transform.position.z =
        (static_cast<double>(rect.y) + static_cast<double>(rect.height - 1) * 0.5) * pixel_size;
    spec.transform.scale.x = static_cast<double>(rect.width) * pixel_size;
    spec.transform.scale.y = pixel_size;
    spec.transform.scale.z = static_cast<double>(rect.height) * pixel_size;
    specs.push_back(std::move(spec));
  }

  return specs;
}

std::vector<DecorationSpec> decoration_specs_from_image_pixels(
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
      if (!is_visible_pixel(image, width, x, y)) continue;

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

std::vector<DecorationSpec> decoration_specs_from_image(
    const PngRgbaImage& image,
    const PixelDecorationImportOptions& options) {
  if (!options.merge_same_color_rects) {
    return decoration_specs_from_image_pixels(image, options.asset_id, options.pixel_size);
  }
  const auto merged_rects = merge_same_color_rects(image);
  return decoration_specs_from_rects(merged_rects, options.asset_id, options.pixel_size);
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
  auto decoration_specs = decoration_specs_from_image(image, options);

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
  summary.merge_same_color_rects = options.merge_same_color_rects;
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
