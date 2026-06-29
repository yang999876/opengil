#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace opengil {

struct PngRgbaImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;
};

PngRgbaImage load_png_rgba_file(const std::filesystem::path& png_path, const char* error_prefix);

}  // namespace opengil
