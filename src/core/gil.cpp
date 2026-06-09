#include "opengil/gil.hpp"

#include <fstream>
#include <stdexcept>

#include "opengil/sha256.hpp"

namespace opengil {

uint32_t read_u32_be(std::span<const uint8_t> bytes, size_t offset) {
  if (offset + 4 > bytes.size()) {
    throw std::runtime_error("read_u32_be out of bounds");
  }
  return (static_cast<uint32_t>(bytes[offset]) << 24) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<uint32_t>(bytes[offset + 3]);
}

GilFile load_gil_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open input file: " + path.string());
  }

  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  stream.seekg(0, std::ios::beg);
  if (size < 24) {
    throw std::runtime_error("file is too small to be a .gil file");
  }

  GilFile file;
  file.path = path;
  file.bytes.resize(static_cast<size_t>(size));
  stream.read(reinterpret_cast<char*>(file.bytes.data()), size);
  if (!stream) {
    throw std::runtime_error("failed to read input file: " + path.string());
  }

  const std::span<const uint8_t> bytes(file.bytes.data(), file.bytes.size());
  file.header.left_size = read_u32_be(bytes, 0);
  file.header.schema = read_u32_be(bytes, 4);
  file.header.head_tag = read_u32_be(bytes, 8);
  file.header.file_type = read_u32_be(bytes, 12);
  file.header.proto_size = read_u32_be(bytes, 16);
  file.header.tail_tag = read_u32_be(bytes, file.bytes.size() - 4);
  return file;
}

std::span<const uint8_t> payload(const GilFile& file) {
  if (file.bytes.size() < 24) return {};
  return std::span<const uint8_t>(file.bytes.data() + 20, file.bytes.size() - 24);
}

std::vector<Field> top_level_fields(const GilFile& file) {
  std::vector<Field> fields;
  std::string error;
  if (!parse_fields(payload(file), fields, &error)) {
    throw std::runtime_error("failed to parse top-level payload: " + error);
  }
  return fields;
}

std::optional<Field> top_level_len_field(const GilFile& file, uint32_t field_number) {
  const auto fields = top_level_fields(file);
  for (const auto& field : fields) {
    if (field.number == field_number && field.wire == 2) return field;
  }
  return std::nullopt;
}

std::optional<std::span<const uint8_t>> top_level_data(const GilFile& file, uint32_t field_number) {
  const auto field = top_level_len_field(file, field_number);
  if (!field) return std::nullopt;
  return field_data(payload(file), *field);
}

ValidationResult validate_gil(const GilFile& file) {
  ValidationResult result;
  result.ok = true;

  if (file.bytes.size() < 24) {
    result.ok = false;
    result.errors.push_back("file is smaller than the 24-byte GIL envelope");
    return result;
  }

  const size_t payload_size = payload(file).size();
  if (file.header.left_size != payload_size + 20) {
    result.ok = false;
    result.errors.push_back("header.leftSize does not equal payloadSize + 20");
  }
  if (file.header.proto_size != payload_size) {
    result.ok = false;
    result.errors.push_back("header.protoSize does not equal payloadSize");
  }

  std::vector<Field> fields;
  std::string error;
  if (!parse_fields(payload(file), fields, &error)) {
    result.ok = false;
    result.errors.push_back("payload parse failed: " + error);
  }

  if (fields.empty()) {
    result.warnings.push_back("payload has no parseable top-level fields");
  }

  return result;
}

std::string file_sha256(const GilFile& file) {
  return sha256_hex(std::span<const uint8_t>(file.bytes.data(), file.bytes.size()));
}

std::string bytes_sha256(std::span<const uint8_t> bytes) {
  return sha256_hex(bytes);
}

}  // namespace opengil

