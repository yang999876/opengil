#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "opengil/wire.hpp"

namespace opengil {

struct GilHeader {
  uint32_t left_size = 0;
  uint32_t schema = 0;
  uint32_t head_tag = 0;
  uint32_t file_type = 0;
  uint32_t proto_size = 0;
  uint32_t tail_tag = 0;
};

struct GilFile {
  std::filesystem::path path;
  std::vector<uint8_t> bytes;
  GilHeader header;
};

struct ValidationResult {
  bool ok = false;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

GilFile load_gil_file(const std::filesystem::path& path);
std::span<const uint8_t> payload(const GilFile& file);
std::vector<Field> top_level_fields(const GilFile& file);
std::optional<Field> top_level_len_field(const GilFile& file, uint32_t field_number);
std::optional<std::span<const uint8_t>> top_level_data(const GilFile& file, uint32_t field_number);
ValidationResult validate_gil(const GilFile& file);

uint32_t read_u32_be(std::span<const uint8_t> bytes, size_t offset);
std::string file_sha256(const GilFile& file);
std::string bytes_sha256(std::span<const uint8_t> bytes);

}  // namespace opengil

