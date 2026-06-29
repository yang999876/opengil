#include "opengil/ui_pixel_import_ops.hpp"

#include <stdexcept>
#include <string>
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

std::vector<UiGeneratedPrimitiveSpec> specs_from_image(const PngRgbaImage& image, double pixel_size) {
  const size_t width = static_cast<size_t>(image.width);
  const size_t height = static_cast<size_t>(image.height);
  const size_t pixel_count = width * height;
  if (pixel_count > kUiPixelImportMaxPixels) throw std::runtime_error("PNG exceeds 10000 pixel import limit");

  std::vector<UiGeneratedPrimitiveSpec> specs;
  specs.reserve(pixel_count);
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t offset = (y * width + x) * 4;
      if (image.pixels[offset + 3] == 0) continue;

      UiGeneratedPrimitiveSpec spec;
      spec.primitive_type_id = kUiPrimitiveRectangle;
      spec.x = static_cast<double>(x) * pixel_size;
      spec.y = static_cast<double>(y) * pixel_size;
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

}  // namespace

UiStructureMutation import_pixel_png_as_ui_primitives(
    const GilFile& file,
    const std::filesystem::path& png_path,
    const UiPixelImportOptions& options) {
  if (options.pixel_size <= 0.0) throw std::runtime_error("pixel-size must be positive");

  const auto image = load_png_rgba_file(png_path, "pixel import");
  auto specs = specs_from_image(image, options.pixel_size);
  if (specs.empty()) {
    const auto list = list_ui_primitives(file, options.target_controller_entry_id);
    UiStructureMutation mutation;
    mutation.payload = std::vector<uint8_t>(payload(file).begin(), payload(file).end());
    mutation.bytes = file.bytes;
    mutation.summary.kind = "importPixelPngUiPrimitives";
    mutation.summary.target_controller_entry_id = options.target_controller_entry_id;
    mutation.summary.primitive_count = list.primitives.size();
    mutation.summary.entry_ids.reserve(mutation.summary.primitive_count);
    for (const auto& primitive : list.primitives) {
      if (primitive.entry_id) mutation.summary.entry_ids.push_back(*primitive.entry_id);
    }
    return mutation;
  }

  UiGeneratedPrimitiveOptions generated_options;
  generated_options.target_controller_entry_id = options.target_controller_entry_id;
  auto mutation = append_generated_ui_primitives(file, specs, generated_options);
  mutation.summary.kind = "importPixelPngUiPrimitives";
  return mutation;
}

}  // namespace opengil
