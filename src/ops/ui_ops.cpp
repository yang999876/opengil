#include "opengil/ui_ops.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "opengil/wire.hpp"

namespace opengil {
namespace {

struct PrimitiveEntry {
  size_t top9_index = 0;
  std::optional<uint64_t> entry_id;
  std::vector<uint8_t> data;
};

template <size_t N>
std::optional<uint64_t> read_varint_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_varint_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

template <size_t N>
std::optional<float> read_fixed32_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_fixed32_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

template <size_t N>
std::optional<std::string> read_string_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_string_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

bool is_ui_primitive_type(uint64_t value) {
  return value == kUiPrimitiveRectangle ||
         value == kUiPrimitiveEllipse ||
         value == kUiPrimitiveTriangle;
}

std::optional<std::vector<uint8_t>> direct_len_data(std::span<const uint8_t> message, uint32_t field_number) {
  const auto field = first_len_field(message, field_number);
  if (!field) return std::nullopt;
  const auto data = field_data(message, *field);
  return std::vector<uint8_t>(data.begin(), data.end());
}

bool is_ui_primitive_message(std::span<const uint8_t> message) {
  const auto primitive31 = direct_len_data(message, 31);
  if (!primitive31) return false;
  const std::array<uint32_t, 1> type_path{2};
  const auto type_id = read_varint_path(std::span<const uint8_t>(primitive31->data(), primitive31->size()), type_path);
  return type_id && is_ui_primitive_type(*type_id);
}

bool contains_ui_primitive_message(std::span<const uint8_t> message) {
  if (is_ui_primitive_message(message)) return true;

  std::vector<Field> fields;
  if (!parse_fields(message, fields)) return false;
  for (const auto& field : fields) {
    if (field.wire != 2) continue;
    if (contains_ui_primitive_message(field_data(message, field))) return true;
  }
  return false;
}

std::optional<uint64_t> top9_entry_id(std::span<const uint8_t> entry) {
  const std::array<uint32_t, 1> id_path{501};
  return read_varint_path(entry, id_path);
}

std::vector<uint64_t> decode_packed_varints(std::span<const uint8_t> data) {
  std::vector<uint64_t> values;
  size_t offset = 0;
  while (offset < data.size()) {
    const auto value = read_varint(data, offset);
    if (!value || value->next <= offset) break;
    values.push_back(value->value);
    offset = value->next;
  }
  return values;
}

std::vector<uint64_t> primitive_entry_ids_from_controller(std::span<const uint8_t> top9, uint64_t controller_entry_id) {
  std::vector<Field> fields;
  if (!parse_fields(top9, fields)) return {};

  for (const auto& field : fields) {
    if (field.number != 502 || field.wire != 2) continue;
    const auto entry = field_data(top9, field);
    if (top9_entry_id(entry) != controller_entry_id) continue;
    const auto packed = direct_len_data(entry, 503);
    if (!packed) return {};
    return decode_packed_varints(std::span<const uint8_t>(packed->data(), packed->size()));
  }

  return {};
}

std::vector<PrimitiveEntry> primitive_entries_from_top9(std::span<const uint8_t> top9) {
  std::vector<PrimitiveEntry> entries;
  std::vector<Field> fields;
  if (!parse_fields(top9, fields)) return entries;

  for (size_t i = 0; i < fields.size(); ++i) {
    const auto& field = fields[i];
    if (field.number != 502 || field.wire != 2) continue;
    const auto entry = field_data(top9, field);
    if (!contains_ui_primitive_message(entry)) continue;

    PrimitiveEntry item;
    item.top9_index = i;
    item.entry_id = top9_entry_id(entry);
    item.data.assign(entry.begin(), entry.end());
    entries.push_back(std::move(item));
  }

  return entries;
}

std::vector<PrimitiveEntry> order_entries(
    const std::vector<PrimitiveEntry>& entries,
    const std::vector<uint64_t>& controller_entry_ids) {
  if (controller_entry_ids.empty()) return entries;

  std::vector<PrimitiveEntry> ordered;
  for (uint64_t entry_id : controller_entry_ids) {
    for (const auto& entry : entries) {
      if (entry.entry_id && *entry.entry_id == entry_id) {
        ordered.push_back(entry);
        break;
      }
    }
  }
  return ordered;
}

int64_t color_from_varint(uint64_t raw) {
  const auto low32 = static_cast<uint32_t>(raw);
  return static_cast<int64_t>(static_cast<int32_t>(low32));
}

void read_direct_snapshot(std::span<const uint8_t> message, UiPrimitive& out) {
  const std::array<uint32_t, 2> name_path{12, 501};
  const std::array<uint32_t, 2> type_path{31, 2};
  const std::array<uint32_t, 2> color_path{31, 4};
  const std::array<uint32_t, 3> layer_path{13, 12, 503};
  const std::array<uint32_t, 6> pos_x_path{13, 12, 501, 502, 504, 501};
  const std::array<uint32_t, 6> pos_y_path{13, 12, 501, 502, 504, 502};
  const std::array<uint32_t, 6> size_w_path{13, 12, 501, 502, 505, 501};
  const std::array<uint32_t, 6> size_h_path{13, 12, 501, 502, 505, 502};
  const std::array<uint32_t, 6> scale_x_path{13, 12, 501, 502, 501, 1};
  const std::array<uint32_t, 6> scale_y_path{13, 12, 501, 502, 501, 2};
  const std::array<uint32_t, 6> scale_z_path{13, 12, 501, 502, 501, 3};
  const std::array<uint32_t, 6> rotation_z_path{13, 12, 501, 502, 508, 3};

  out.name = read_string_path(message, name_path);
  out.primitive_type_id = read_varint_path(message, type_path);
  out.raw_color = read_varint_path(message, color_path);
  if (out.raw_color) out.color = color_from_varint(*out.raw_color);
  out.layer = read_varint_path(message, layer_path);
  if (auto value = read_fixed32_path(message, pos_x_path)) out.transform.position.x = *value;
  if (auto value = read_fixed32_path(message, pos_y_path)) out.transform.position.y = *value;
  if (auto value = read_fixed32_path(message, size_w_path)) out.transform.size.x = *value;
  if (auto value = read_fixed32_path(message, size_h_path)) out.transform.size.y = *value;
  if (auto value = read_fixed32_path(message, scale_x_path)) out.transform.scale.x = *value;
  if (auto value = read_fixed32_path(message, scale_y_path)) out.transform.scale.y = *value;
  if (auto value = read_fixed32_path(message, scale_z_path)) out.transform.scale.z = *value;
  if (auto value = read_fixed32_path(message, rotation_z_path)) out.transform.rotation_z = *value;
}

bool snapshot_has_core_data(const UiPrimitive& primitive) {
  return primitive.primitive_type_id || primitive.raw_color || primitive.name;
}

void read_wrapped_snapshot(std::span<const uint8_t> message, UiPrimitive& out) {
  const std::array<uint32_t, 3> name_path{505, 12, 501};
  const std::array<uint32_t, 4> type_path{505, 503, 31, 2};
  const std::array<uint32_t, 4> color_path{505, 503, 31, 4};
  const std::array<uint32_t, 5> layer_path{505, 503, 13, 12, 503};
  const std::array<uint32_t, 8> pos_x_path{505, 503, 13, 12, 501, 502, 504, 501};
  const std::array<uint32_t, 8> pos_y_path{505, 503, 13, 12, 501, 502, 504, 502};
  const std::array<uint32_t, 8> size_w_path{505, 503, 13, 12, 501, 502, 505, 501};
  const std::array<uint32_t, 8> size_h_path{505, 503, 13, 12, 501, 502, 505, 502};
  const std::array<uint32_t, 8> scale_x_path{505, 503, 13, 12, 501, 502, 501, 1};
  const std::array<uint32_t, 8> scale_y_path{505, 503, 13, 12, 501, 502, 501, 2};
  const std::array<uint32_t, 8> scale_z_path{505, 503, 13, 12, 501, 502, 501, 3};
  const std::array<uint32_t, 8> rotation_z_path{505, 503, 13, 12, 501, 502, 508, 3};

  out.name = read_string_path(message, name_path);
  out.primitive_type_id = read_varint_path(message, type_path);
  out.raw_color = read_varint_path(message, color_path);
  if (out.raw_color) out.color = color_from_varint(*out.raw_color);
  out.layer = read_varint_path(message, layer_path);
  if (auto value = read_fixed32_path(message, pos_x_path)) out.transform.position.x = *value;
  if (auto value = read_fixed32_path(message, pos_y_path)) out.transform.position.y = *value;
  if (auto value = read_fixed32_path(message, size_w_path)) out.transform.size.x = *value;
  if (auto value = read_fixed32_path(message, size_h_path)) out.transform.size.y = *value;
  if (auto value = read_fixed32_path(message, scale_x_path)) out.transform.scale.x = *value;
  if (auto value = read_fixed32_path(message, scale_y_path)) out.transform.scale.y = *value;
  if (auto value = read_fixed32_path(message, scale_z_path)) out.transform.scale.z = *value;
  if (auto value = read_fixed32_path(message, rotation_z_path)) out.transform.rotation_z = *value;
}

UiPrimitive read_snapshot(const PrimitiveEntry& entry, size_t primitive_index) {
  UiPrimitive primitive;
  primitive.primitive_index = primitive_index;
  primitive.top9_index = entry.top9_index;
  primitive.entry_id = entry.entry_id;
  const std::array<uint32_t, 1> controller_path{504};
  primitive.controller_entry_id = read_varint_path(std::span<const uint8_t>(entry.data.data(), entry.data.size()), controller_path);

  read_direct_snapshot(std::span<const uint8_t>(entry.data.data(), entry.data.size()), primitive);
  if (!snapshot_has_core_data(primitive)) {
    read_wrapped_snapshot(std::span<const uint8_t>(entry.data.data(), entry.data.size()), primitive);
  }
  return primitive;
}

}  // namespace

UiPrimitiveList list_ui_primitives(const GilFile& file, uint64_t controller_entry_id) {
  UiPrimitiveList list;
  list.controller_entry_id = controller_entry_id;
  list.has_top9 = top_level_data(file, 9).has_value();
  list.has_top46 = top_level_data(file, 46).has_value();

  const auto top9 = top_level_data(file, 9);
  if (!top9) return list;

  const auto entries = primitive_entries_from_top9(*top9);
  const auto controller_entry_ids = primitive_entry_ids_from_controller(*top9, controller_entry_id);
  const auto ordered_entries = order_entries(entries, controller_entry_ids);

  list.primitives.reserve(ordered_entries.size());
  for (size_t i = 0; i < ordered_entries.size(); ++i) {
    list.primitives.push_back(read_snapshot(ordered_entries[i], i));
  }

  return list;
}

}  // namespace opengil
