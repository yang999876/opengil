#include "opengil/wire.hpp"

#include <cstring>
#include <sstream>

namespace opengil {

std::optional<VarintRead> read_varint(std::span<const uint8_t> bytes, size_t offset) {
  uint64_t value = 0;
  uint32_t shift = 0;
  size_t current = offset;
  while (current < bytes.size() && shift < 64) {
    const uint8_t byte = bytes[current++];
    value |= static_cast<uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) return VarintRead{value, current};
    shift += 7;
  }
  return std::nullopt;
}

std::vector<uint8_t> encode_varint(uint64_t value) {
  std::vector<uint8_t> bytes;
  while (value >= 0x80) {
    bytes.push_back(static_cast<uint8_t>((value & 0x7f) | 0x80));
    value >>= 7;
  }
  bytes.push_back(static_cast<uint8_t>(value));
  return bytes;
}

std::vector<uint8_t> encode_key(uint32_t field_number, uint8_t wire) {
  return encode_varint((static_cast<uint64_t>(field_number) << 3) | wire);
}

std::vector<uint8_t> encode_len_field(uint32_t field_number, std::span<const uint8_t> payload) {
  std::vector<uint8_t> out = encode_key(field_number, static_cast<uint8_t>(WireType::LengthDelimited));
  std::vector<uint8_t> len = encode_varint(payload.size());
  out.insert(out.end(), len.begin(), len.end());
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

std::vector<uint8_t> encode_field(const OwnedField& field) {
  std::vector<uint8_t> out = encode_key(field.number, field.wire);
  if (field.wire == 0) {
    auto value = encode_varint(field.varint);
    out.insert(out.end(), value.begin(), value.end());
    return out;
  }
  if (field.wire == 2) {
    auto len = encode_varint(field.data.size());
    out.insert(out.end(), len.begin(), len.end());
    out.insert(out.end(), field.data.begin(), field.data.end());
    return out;
  }
  if (field.wire == 1 || field.wire == 5) {
    out.insert(out.end(), field.data.begin(), field.data.end());
    return out;
  }
  return out;
}

bool parse_fields(std::span<const uint8_t> bytes, std::vector<Field>& out, std::string* error) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    const size_t raw_start = offset;
    const auto key = read_varint(bytes, offset);
    if (!key) {
      if (error) *error = "failed to read field key varint";
      return false;
    }
    offset = key->next;

    Field field;
    field.number = static_cast<uint32_t>(key->value >> 3);
    field.wire = static_cast<uint8_t>(key->value & 7);
    field.raw_start = raw_start;

    switch (field.wire) {
      case 0: {
        const auto value = read_varint(bytes, offset);
        if (!value) {
          if (error) *error = "failed to read varint field value";
          return false;
        }
        field.varint = value->value;
        offset = value->next;
        field.raw_end = offset;
        out.push_back(field);
        break;
      }
      case 1: {
        if (offset + 8 > bytes.size()) {
          if (error) *error = "fixed64 field exceeds message bounds";
          return false;
        }
        field.data_start = offset;
        field.data_end = offset + 8;
        offset += 8;
        field.raw_end = offset;
        out.push_back(field);
        break;
      }
      case 2: {
        const auto len = read_varint(bytes, offset);
        if (!len) {
          if (error) *error = "failed to read length-delimited size";
          return false;
        }
        field.data_start = len->next;
        field.data_end = field.data_start + static_cast<size_t>(len->value);
        if (field.data_end > bytes.size()) {
          if (error) *error = "length-delimited field exceeds message bounds";
          return false;
        }
        offset = field.data_end;
        field.raw_end = offset;
        out.push_back(field);
        break;
      }
      case 5: {
        if (offset + 4 > bytes.size()) {
          if (error) *error = "fixed32 field exceeds message bounds";
          return false;
        }
        field.data_start = offset;
        field.data_end = offset + 4;
        offset += 4;
        field.raw_end = offset;
        out.push_back(field);
        break;
      }
      default:
        if (error) {
          std::ostringstream msg;
          msg << "unsupported wire type " << static_cast<int>(field.wire)
              << " at offset " << raw_start;
          *error = msg.str();
        }
        return false;
    }
  }

  return true;
}

OwnedField clone_owned_field(std::span<const uint8_t> message, const Field& field) {
  OwnedField out;
  out.number = field.number;
  out.wire = field.wire;
  out.varint = field.varint;
  if (field.wire == 1 || field.wire == 2 || field.wire == 5) {
    const auto data = field_data(message, field);
    out.data.assign(data.begin(), data.end());
  }
  return out;
}

std::vector<OwnedField> parse_owned_fields(std::span<const uint8_t> bytes, std::string* error) {
  std::vector<Field> fields;
  if (!parse_fields(bytes, fields, error)) return {};
  std::vector<OwnedField> out;
  out.reserve(fields.size());
  for (const auto& field : fields) out.push_back(clone_owned_field(bytes, field));
  return out;
}

std::vector<uint8_t> rebuild_message(const std::vector<OwnedField>& fields) {
  std::vector<uint8_t> out;
  for (const auto& field : fields) {
    auto encoded = encode_field(field);
    out.insert(out.end(), encoded.begin(), encoded.end());
  }
  return out;
}

std::optional<Field> first_len_field(std::span<const uint8_t> bytes, uint32_t field_number) {
  std::vector<Field> fields;
  if (!parse_fields(bytes, fields)) return std::nullopt;
  for (const auto& field : fields) {
    if (field.number == field_number && field.wire == 2) return field;
  }
  return std::nullopt;
}

std::vector<Field> len_fields(std::span<const uint8_t> bytes, uint32_t field_number) {
  std::vector<Field> fields;
  std::vector<Field> result;
  if (!parse_fields(bytes, fields)) return result;
  for (const auto& field : fields) {
    if (field.number == field_number && field.wire == 2) result.push_back(field);
  }
  return result;
}

std::span<const uint8_t> field_data(std::span<const uint8_t> message, const Field& field) {
  return message.subspan(field.data_start, field.data_end - field.data_start);
}

std::optional<uint64_t> read_varint_at_path(std::span<const uint8_t> message, std::span<const uint32_t> path) {
  if (path.empty()) return std::nullopt;
  std::vector<Field> fields;
  if (!parse_fields(message, fields)) return std::nullopt;
  for (const auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire == 0) return field.varint;
      continue;
    }
    if (field.wire != 2) continue;
    const auto nested = read_varint_at_path(field_data(message, field), path.subspan(1));
    if (nested) return nested;
  }
  return std::nullopt;
}

std::optional<std::string> read_string_at_path(std::span<const uint8_t> message, std::span<const uint32_t> path) {
  if (path.empty()) return std::nullopt;
  std::vector<Field> fields;
  if (!parse_fields(message, fields)) return std::nullopt;
  for (const auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 2) continue;
      const auto data = field_data(message, field);
      return std::string(reinterpret_cast<const char*>(data.data()), data.size());
    }
    if (field.wire != 2) continue;
    const auto nested = read_string_at_path(field_data(message, field), path.subspan(1));
    if (nested) return nested;
  }
  return std::nullopt;
}

std::optional<float> read_fixed32_at_path(std::span<const uint8_t> message, std::span<const uint32_t> path) {
  if (path.empty()) return std::nullopt;
  std::vector<Field> fields;
  if (!parse_fields(message, fields)) return std::nullopt;
  for (const auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 5 || field.data_end - field.data_start != 4) continue;
      float value = 0.0f;
      const auto data = field_data(message, field);
      std::memcpy(&value, data.data(), sizeof(float));
      return value;
    }
    if (field.wire != 2) continue;
    const auto nested = read_fixed32_at_path(field_data(message, field), path.subspan(1));
    if (nested) return nested;
  }
  return std::nullopt;
}

std::string normalize_visible_text(std::string value) {
  while (!value.empty() && (value.front() == '\r' || value.front() == '\n')) {
    value.erase(value.begin());
  }
  return value;
}

bool paths_equal(std::span<const uint32_t> lhs, std::initializer_list<uint32_t> rhs) {
  if (lhs.size() != rhs.size()) return false;
  size_t i = 0;
  for (uint32_t value : rhs) {
    if (lhs[i++] != value) return false;
  }
  return true;
}

}  // namespace opengil
