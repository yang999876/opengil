#include "opengil/gil.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "opengil/sha256.hpp"

namespace opengil {

namespace {

void write_u32_be(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  bytes[offset] = static_cast<uint8_t>((value >> 24) & 0xff);
  bytes[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xff);
  bytes[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xff);
  bytes[offset + 3] = static_cast<uint8_t>(value & 0xff);
}

}  // namespace

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

std::vector<uint8_t> build_gil_bytes(const GilHeader& header, std::span<const uint8_t> payload_bytes) {
  std::vector<uint8_t> bytes(payload_bytes.size() + 24);
  write_u32_be(bytes, 0, static_cast<uint32_t>(payload_bytes.size() + 20));
  write_u32_be(bytes, 4, header.schema);
  write_u32_be(bytes, 8, header.head_tag);
  write_u32_be(bytes, 12, header.file_type);
  write_u32_be(bytes, 16, static_cast<uint32_t>(payload_bytes.size()));
  std::copy(payload_bytes.begin(), payload_bytes.end(), bytes.begin() + 20);
  write_u32_be(bytes, bytes.size() - 4, header.tail_tag);
  return bytes;
}

void save_gil_file(const std::filesystem::path& path, const GilHeader& header, std::span<const uint8_t> payload_bytes) {
  const auto bytes = build_gil_bytes(header, payload_bytes);
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open output file: " + path.string());
  }
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!stream) {
    throw std::runtime_error("failed to write output file: " + path.string());
  }
}

std::vector<uint8_t> replace_top_level_field_data(
    std::span<const uint8_t> payload_bytes,
    uint32_t field_number,
    std::span<const uint8_t> new_data) {
  std::string error;
  auto fields = parse_owned_fields(payload_bytes, &error);
  if (!error.empty()) {
    throw std::runtime_error("failed to parse payload for replacement: " + error);
  }
  bool changed = false;
  for (auto& field : fields) {
    if (!changed && field.number == field_number && field.wire == 2) {
      field.data.assign(new_data.begin(), new_data.end());
      changed = true;
    }
  }
  if (!changed) {
    throw std::runtime_error("top-level field not found for replacement");
  }
  return rebuild_message(fields);
}

std::string file_sha256(const GilFile& file) {
  return sha256_hex(std::span<const uint8_t>(file.bytes.data(), file.bytes.size()));
}

std::string bytes_sha256(std::span<const uint8_t> bytes) {
  return sha256_hex(bytes);
}

}  // namespace opengil
