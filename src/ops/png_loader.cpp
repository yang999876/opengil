#include "png_loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace opengil {
namespace {

struct StbiImage {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = nullptr;

  ~StbiImage() {
    stbi_image_free(pixels);
  }
};

std::vector<uint8_t> read_binary_file(const std::filesystem::path& path, const char* error_prefix) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) throw std::runtime_error(std::string(error_prefix) + ": failed to open PNG file");
  const auto size = stream.tellg();
  if (size < 0) throw std::runtime_error(std::string(error_prefix) + ": failed to read PNG file size");

  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  stream.seekg(0);
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!stream) throw std::runtime_error(std::string(error_prefix) + ": failed to read PNG file");
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

}  // namespace

PngRgbaImage load_png_rgba_file(const std::filesystem::path& png_path, const char* error_prefix) {
  if (lower_extension(png_path) != ".png") throw std::runtime_error(std::string(error_prefix) + ": only accepts .png files");
  auto bytes = read_binary_file(png_path, error_prefix);
  if (!has_png_signature(bytes)) throw std::runtime_error(std::string(error_prefix) + ": input is not a PNG file");

  StbiImage decoded;
  decoded.pixels = stbi_load_from_memory(
      bytes.data(),
      static_cast<int>(bytes.size()),
      &decoded.width,
      &decoded.height,
      &decoded.channels,
      4);
  if (!decoded.pixels) throw std::runtime_error(std::string(error_prefix) + ": failed to decode PNG file");
  if (decoded.width <= 0 || decoded.height <= 0) throw std::runtime_error(std::string(error_prefix) + ": PNG dimensions must be positive");

  const size_t byte_count = static_cast<size_t>(decoded.width) * static_cast<size_t>(decoded.height) * 4;
  PngRgbaImage image;
  image.width = decoded.width;
  image.height = decoded.height;
  image.pixels.assign(decoded.pixels, decoded.pixels + byte_count);
  return image;
}

}  // namespace opengil
