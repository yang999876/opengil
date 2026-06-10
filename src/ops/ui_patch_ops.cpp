#include "opengil/ui_patch_ops.hpp"

#include <array>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include "opengil/wire.hpp"

namespace opengil {
namespace {

OwnedField make_len_field(uint32_t number, std::vector<uint8_t> data) {
  OwnedField field;
  field.number = number;
  field.wire = 2;
  field.data = std::move(data);
  return field;
}

void set_varint_path(std::vector<OwnedField>& fields, std::span<const uint32_t> path, uint64_t value) {
  if (path.empty()) throw std::runtime_error("empty varint path");
  for (auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 0) continue;
      field.varint = value;
      return;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "ui varint child");
    try {
      set_varint_path(child_fields, path.subspan(1), value);
      field.data = rebuild_message(child_fields);
      return;
    } catch (const std::runtime_error&) {
      continue;
    }
  }
  throw std::runtime_error("ui varint path not found");
}

void set_len_path(std::vector<OwnedField>& fields, std::span<const uint32_t> path, std::vector<uint8_t> data) {
  if (path.empty()) throw std::runtime_error("empty len path");
  for (auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 2) continue;
      field.data = std::move(data);
      return;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "ui len child");
    try {
      set_len_path(child_fields, path.subspan(1), data);
      field.data = rebuild_message(child_fields);
      return;
    } catch (const std::runtime_error&) {
      continue;
    }
  }
  throw std::runtime_error("ui len path not found");
}

void upsert_varint_leaf_path(std::vector<OwnedField>& fields, std::span<const uint32_t> path, uint64_t value) {
  if (path.empty()) throw std::runtime_error("empty varint path");
  for (auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 0) continue;
      field.varint = value;
      return;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "ui varint child");
    try {
      upsert_varint_leaf_path(child_fields, path.subspan(1), value);
      field.data = rebuild_message(child_fields);
      return;
    } catch (const std::runtime_error&) {
      continue;
    }
  }
  if (path.size() == 1) {
    OwnedField field;
    field.number = path[0];
    field.wire = 0;
    field.varint = value;
    fields.push_back(field);
    return;
  }
  throw std::runtime_error("ui varint path not found");
}

void upsert_fixed32_leaf_path(std::vector<OwnedField>& fields, std::span<const uint32_t> path, double value) {
  if (path.empty()) throw std::runtime_error("empty fixed32 path");
  for (auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 5) continue;
      const auto raw_value = static_cast<float>(value);
      uint32_t raw = 0;
      std::memcpy(&raw, &raw_value, sizeof(float));
      field.data = {
          static_cast<uint8_t>(raw & 0xff),
          static_cast<uint8_t>((raw >> 8) & 0xff),
          static_cast<uint8_t>((raw >> 16) & 0xff),
          static_cast<uint8_t>((raw >> 24) & 0xff),
      };
      return;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "ui fixed32 child");
    try {
      upsert_fixed32_leaf_path(child_fields, path.subspan(1), value);
      field.data = rebuild_message(child_fields);
      return;
    } catch (const std::runtime_error&) {
      continue;
    }
  }
  if (path.size() == 1) {
    const auto raw_value = static_cast<float>(value);
    uint32_t raw = 0;
    std::memcpy(&raw, &raw_value, sizeof(float));
    OwnedField field;
    field.number = path[0];
    field.wire = 5;
    field.data = {
        static_cast<uint8_t>(raw & 0xff),
        static_cast<uint8_t>((raw >> 8) & 0xff),
        static_cast<uint8_t>((raw >> 16) & 0xff),
        static_cast<uint8_t>((raw >> 24) & 0xff),
    };
    fields.push_back(field);
    return;
  }
  throw std::runtime_error("ui fixed32 path not found");
}

std::vector<uint8_t> string_bytes(const std::string& value) {
  return std::vector<uint8_t>(value.begin(), value.end());
}

uint64_t raw_color_from_signed(int64_t color) {
  return static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(color)));
}

GilFile file_from_mutation(const GilFile& source, const std::vector<uint8_t>& bytes) {
  GilFile file;
  file.path = source.path;
  file.header = source.header;
  file.bytes = bytes;
  return file;
}

UiPrimitivePatchMutation patch_ui_primitive(
    const GilFile& file,
    size_t primitive_index,
    uint64_t controller_entry_id,
    std::string kind,
    const std::function<std::vector<uint8_t>(std::span<const uint8_t>)>& patcher) {
  const auto top9 = top_level_data(file, 9);
  if (!top9) throw std::runtime_error("top-level field 9 not found");

  const auto before_list = list_ui_primitives(file, controller_entry_id);
  if (primitive_index >= before_list.primitives.size()) throw std::runtime_error("ui primitive index not found");
  const auto before = before_list.primitives[primitive_index];

  auto fields = parse_owned_fields_or_throw(*top9, "ui top9");
  if (before.top9_index >= fields.size()) throw std::runtime_error("ui primitive top9 index out of range");
  auto& target = fields[before.top9_index];
  if (target.number != 502 || target.wire != 2) throw std::runtime_error("ui primitive top9 entry is not field 502");
  target.data = patcher(std::span<const uint8_t>(target.data.data(), target.data.size()));

  auto next_payload = replace_top_level_field_data(payload(file), 9, rebuild_message(fields));
  auto next_bytes = build_gil_bytes(file.header, next_payload);
  const auto after_file = file_from_mutation(file, next_bytes);
  const auto after_list = list_ui_primitives(after_file, controller_entry_id);
  if (primitive_index >= after_list.primitives.size()) throw std::runtime_error("patched ui primitive index not found");

  UiPrimitivePatchMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = std::move(next_bytes);
  mutation.summary.kind = std::move(kind);
  mutation.summary.primitive_index = primitive_index;
  mutation.summary.entry_id = before.entry_id;
  mutation.summary.before = before;
  mutation.summary.after = after_list.primitives[primitive_index];
  mutation.summary.changed_top_fields = {9};
  return mutation;
}

}  // namespace

UiPrimitivePatchMutation set_ui_primitive_type(
    const GilFile& file,
    size_t primitive_index,
    uint64_t primitive_type_id,
    uint64_t controller_entry_id) {
  return patch_ui_primitive(file, primitive_index, controller_entry_id, "uiSetType", [primitive_type_id](std::span<const uint8_t> entry) {
    auto fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
    const std::array<uint32_t, 2> path{31, 2};
    try {
      set_varint_path(fields, path, primitive_type_id);
    } catch (const std::runtime_error&) {
      fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
      const std::array<uint32_t, 4> wrapped_path{505, 503, 31, 2};
      set_varint_path(fields, wrapped_path, primitive_type_id);
    }
    return rebuild_message(fields);
  });
}

UiPrimitivePatchMutation set_ui_primitive_color(
    const GilFile& file,
    size_t primitive_index,
    int64_t color,
    uint64_t controller_entry_id) {
  return patch_ui_primitive(file, primitive_index, controller_entry_id, "uiSetColor", [color](std::span<const uint8_t> entry) {
    auto fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
    const std::array<uint32_t, 2> path{31, 4};
    try {
      set_varint_path(fields, path, raw_color_from_signed(color));
    } catch (const std::runtime_error&) {
      fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
      const std::array<uint32_t, 4> wrapped_path{505, 503, 31, 4};
      set_varint_path(fields, wrapped_path, raw_color_from_signed(color));
    }
    return rebuild_message(fields);
  });
}

UiPrimitivePatchMutation set_ui_primitive_transform(
    const GilFile& file,
    size_t primitive_index,
    const UiPrimitiveTransform& transform,
    uint64_t controller_entry_id) {
  return patch_ui_primitive(file, primitive_index, controller_entry_id, "uiSetTransform", [&transform](std::span<const uint8_t> entry) {
    auto fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
    const auto apply_direct = [&transform](std::vector<OwnedField>& target_fields) {
      const std::array<uint32_t, 6> scale_x_path{13, 12, 501, 502, 501, 1};
      const std::array<uint32_t, 6> scale_y_path{13, 12, 501, 502, 501, 2};
      const std::array<uint32_t, 6> scale_z_path{13, 12, 501, 502, 501, 3};
      const std::array<uint32_t, 6> pos_x_path{13, 12, 501, 502, 504, 501};
      const std::array<uint32_t, 6> pos_y_path{13, 12, 501, 502, 504, 502};
      const std::array<uint32_t, 6> size_w_path{13, 12, 501, 502, 505, 501};
      const std::array<uint32_t, 6> size_h_path{13, 12, 501, 502, 505, 502};
      const std::array<uint32_t, 6> rotation_z_path{13, 12, 501, 502, 508, 3};
      if (transform.scale.x) upsert_fixed32_leaf_path(target_fields, scale_x_path, *transform.scale.x);
      if (transform.scale.y) upsert_fixed32_leaf_path(target_fields, scale_y_path, *transform.scale.y);
      if (transform.scale.z) upsert_fixed32_leaf_path(target_fields, scale_z_path, *transform.scale.z);
      if (transform.position.x) upsert_fixed32_leaf_path(target_fields, pos_x_path, *transform.position.x);
      if (transform.position.y) upsert_fixed32_leaf_path(target_fields, pos_y_path, *transform.position.y);
      if (transform.size.x) upsert_fixed32_leaf_path(target_fields, size_w_path, *transform.size.x);
      if (transform.size.y) upsert_fixed32_leaf_path(target_fields, size_h_path, *transform.size.y);
      if (transform.rotation_z) upsert_fixed32_leaf_path(target_fields, rotation_z_path, *transform.rotation_z);
    };
    const auto apply_wrapped = [&transform](std::vector<OwnedField>& target_fields) {
      const std::array<uint32_t, 8> scale_x_path{505, 503, 13, 12, 501, 502, 501, 1};
      const std::array<uint32_t, 8> scale_y_path{505, 503, 13, 12, 501, 502, 501, 2};
      const std::array<uint32_t, 8> scale_z_path{505, 503, 13, 12, 501, 502, 501, 3};
      const std::array<uint32_t, 8> pos_x_path{505, 503, 13, 12, 501, 502, 504, 501};
      const std::array<uint32_t, 8> pos_y_path{505, 503, 13, 12, 501, 502, 504, 502};
      const std::array<uint32_t, 8> size_w_path{505, 503, 13, 12, 501, 502, 505, 501};
      const std::array<uint32_t, 8> size_h_path{505, 503, 13, 12, 501, 502, 505, 502};
      const std::array<uint32_t, 8> rotation_z_path{505, 503, 13, 12, 501, 502, 508, 3};
      if (transform.scale.x) upsert_fixed32_leaf_path(target_fields, scale_x_path, *transform.scale.x);
      if (transform.scale.y) upsert_fixed32_leaf_path(target_fields, scale_y_path, *transform.scale.y);
      if (transform.scale.z) upsert_fixed32_leaf_path(target_fields, scale_z_path, *transform.scale.z);
      if (transform.position.x) upsert_fixed32_leaf_path(target_fields, pos_x_path, *transform.position.x);
      if (transform.position.y) upsert_fixed32_leaf_path(target_fields, pos_y_path, *transform.position.y);
      if (transform.size.x) upsert_fixed32_leaf_path(target_fields, size_w_path, *transform.size.x);
      if (transform.size.y) upsert_fixed32_leaf_path(target_fields, size_h_path, *transform.size.y);
      if (transform.rotation_z) upsert_fixed32_leaf_path(target_fields, rotation_z_path, *transform.rotation_z);
    };
    try {
      apply_direct(fields);
    } catch (const std::runtime_error&) {
      fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
      apply_wrapped(fields);
    }
    return rebuild_message(fields);
  });
}

UiPrimitivePatchMutation set_ui_primitive_layer(
    const GilFile& file,
    size_t primitive_index,
    uint64_t layer,
    uint64_t controller_entry_id) {
  return patch_ui_primitive(file, primitive_index, controller_entry_id, "uiSetLayer", [layer](std::span<const uint8_t> entry) {
    auto fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
    const std::array<uint32_t, 3> path{13, 12, 503};
    try {
      upsert_varint_leaf_path(fields, path, layer);
    } catch (const std::runtime_error&) {
      fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
      const std::array<uint32_t, 5> wrapped_path{505, 503, 13, 12, 503};
      upsert_varint_leaf_path(fields, wrapped_path, layer);
    }
    return rebuild_message(fields);
  });
}

UiPrimitivePatchMutation set_ui_primitive_name(
    const GilFile& file,
    size_t primitive_index,
    const std::string& name,
    uint64_t controller_entry_id) {
  return patch_ui_primitive(file, primitive_index, controller_entry_id, "uiSetName", [&name](std::span<const uint8_t> entry) {
    auto fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
    const std::array<uint32_t, 2> path{12, 501};
    try {
      set_len_path(fields, path, string_bytes(name));
    } catch (const std::runtime_error&) {
      fields = parse_owned_fields_or_throw(entry, "ui primitive entry");
      const std::array<uint32_t, 3> wrapped_path{505, 12, 501};
      set_len_path(fields, wrapped_path, string_bytes(name));
    }
    return rebuild_message(fields);
  });
}

}  // namespace opengil
