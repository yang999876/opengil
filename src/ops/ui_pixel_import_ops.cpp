#include "opengil/ui_pixel_import_ops.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "opengil/ui_generated_ops.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace opengil {
namespace {

struct StbiImage {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = nullptr;

  StbiImage() = default;

  ~StbiImage() {
    stbi_image_free(pixels);
  }

  StbiImage(const StbiImage&) = delete;
  StbiImage& operator=(const StbiImage&) = delete;

  StbiImage(StbiImage&& other) noexcept
      : width(other.width), height(other.height), channels(other.channels), pixels(other.pixels) {
    other.pixels = nullptr;
  }

  StbiImage& operator=(StbiImage&& other) noexcept {
    if (this == &other) return *this;
    stbi_image_free(pixels);
    width = other.width;
    height = other.height;
    channels = other.channels;
    pixels = other.pixels;
    other.pixels = nullptr;
    return *this;
  }
};

std::vector<uint8_t> read_binary_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) throw std::runtime_error("failed to open PNG file");
  const auto size = stream.tellg();
  if (size < 0) throw std::runtime_error("failed to read PNG file size");

  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  stream.seekg(0);
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!stream) throw std::runtime_error("failed to read PNG file");
  return bytes;
}

std::string lower_extension(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return ext;
}

bool has_png_signature(std::span<const uint8_t> bytes) {
  constexpr uint8_t kPngSignature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  return bytes.size() >= std::size(kPngSignature) &&
         std::equal(std::begin(kPngSignature), std::end(kPngSignature), bytes.begin());
}

StbiImage load_png_rgba(const std::filesystem::path& png_path) {
  if (lower_extension(png_path) != ".png") throw std::runtime_error("pixel import only accepts .png files");
  auto bytes = read_binary_file(png_path);
  if (!has_png_signature(bytes)) throw std::runtime_error("pixel import input is not a PNG file");

  StbiImage image;
  image.pixels = stbi_load_from_memory(
      bytes.data(),
      static_cast<int>(bytes.size()),
      &image.width,
      &image.height,
      &image.channels,
      4);
  if (!image.pixels) throw std::runtime_error("failed to decode PNG file");
  if (image.width <= 0 || image.height <= 0) throw std::runtime_error("PNG dimensions must be positive");
  return image;
}

int64_t argb_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  const uint32_t raw = (static_cast<uint32_t>(a) << 24) |
                       (static_cast<uint32_t>(r) << 16) |
                       (static_cast<uint32_t>(g) << 8) |
                       static_cast<uint32_t>(b);
  return static_cast<int64_t>(static_cast<int32_t>(raw));
}

std::vector<UiGeneratedPrimitiveSpec> specs_from_image(const StbiImage& image, double pixel_size) {
  const size_t width = static_cast<size_t>(image.width);
  const size_t height = static_cast<size_t>(image.height);
  const size_t pixel_count = width * height;
  if (pixel_count > kUiPixelImportMaxPixels) throw std::runtime_error("PNG exceeds 10000 pixel import limit");

  std::vector<UiGeneratedPrimitiveSpec> specs;
  specs.reserve(pixel_count);
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t offset = (y * width + x) * 4;
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

  const auto image = load_png_rgba(png_path);
  auto specs = specs_from_image(image, options.pixel_size);

  UiGeneratedPrimitiveOptions generated_options;
  generated_options.target_controller_entry_id = options.target_controller_entry_id;
  auto mutation = append_generated_ui_primitives(file, specs, generated_options);
  mutation.summary.kind = "importPixelPngUiPrimitives";
  return mutation;
}

}  // namespace opengil
