#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opengil {

enum class WireType : uint8_t {
  Varint = 0,
  Fixed64 = 1,
  LengthDelimited = 2,
  Fixed32 = 5,
};

struct Field {
  uint32_t number = 0;
  uint8_t wire = 0;
  size_t raw_start = 0;
  size_t raw_end = 0;
  size_t data_start = 0;
  size_t data_end = 0;
  uint64_t varint = 0;
};

struct OwnedField {
  uint32_t number = 0;
  uint8_t wire = 0;
  uint64_t varint = 0;
  std::vector<uint8_t> data;
};

struct VarintRead {
  uint64_t value = 0;
  size_t next = 0;
};

std::optional<VarintRead> read_varint(std::span<const uint8_t> bytes, size_t offset);
std::vector<uint8_t> encode_varint(uint64_t value);
std::vector<uint8_t> encode_key(uint32_t field_number, uint8_t wire);
std::vector<uint8_t> encode_len_field(uint32_t field_number, std::span<const uint8_t> payload);
std::vector<uint8_t> encode_field(const OwnedField& field);

bool parse_fields(std::span<const uint8_t> bytes, std::vector<Field>& out, std::string* error = nullptr);
std::vector<OwnedField> parse_owned_fields(std::span<const uint8_t> bytes, std::string* error = nullptr);
std::vector<uint8_t> rebuild_message(const std::vector<OwnedField>& fields);
OwnedField clone_owned_field(std::span<const uint8_t> message, const Field& field);
std::optional<Field> first_len_field(std::span<const uint8_t> bytes, uint32_t field_number);
std::vector<Field> len_fields(std::span<const uint8_t> bytes, uint32_t field_number);
std::span<const uint8_t> field_data(std::span<const uint8_t> message, const Field& field);

std::optional<uint64_t> read_varint_at_path(std::span<const uint8_t> message, std::span<const uint32_t> path);
std::optional<std::string> read_string_at_path(std::span<const uint8_t> message, std::span<const uint32_t> path);
std::optional<float> read_fixed32_at_path(std::span<const uint8_t> message, std::span<const uint32_t> path);

std::string normalize_visible_text(std::string value);
bool paths_equal(std::span<const uint32_t> lhs, std::initializer_list<uint32_t> rhs);

}  // namespace opengil
