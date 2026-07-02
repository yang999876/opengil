#include "opengil/ui_generated_ops.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include "opengil/wire.hpp"

namespace opengil {
namespace {

struct Top9Entry {
  uint64_t entry_id = 0;
  std::vector<uint8_t> data;
};

std::span<const uint8_t> bytes_span(const std::vector<uint8_t>& bytes) {
  return std::span<const uint8_t>(bytes.data(), bytes.size());
}

OwnedField varint_field(uint32_t number, uint64_t value) {
  OwnedField field;
  field.number = number;
  field.wire = 0;
  field.varint = value;
  return field;
}

OwnedField len_field(uint32_t number, std::vector<uint8_t> data) {
  OwnedField field;
  field.number = number;
  field.wire = 2;
  field.data = std::move(data);
  return field;
}

OwnedField fixed32_field(uint32_t number, double value) {
  const auto raw_value = static_cast<float>(value);
  uint32_t raw = 0;
  std::memcpy(&raw, &raw_value, sizeof(float));
  OwnedField field;
  field.number = number;
  field.wire = 5;
  field.data = {
      static_cast<uint8_t>(raw & 0xff),
      static_cast<uint8_t>((raw >> 8) & 0xff),
      static_cast<uint8_t>((raw >> 16) & 0xff),
      static_cast<uint8_t>((raw >> 24) & 0xff),
  };
  return field;
}

std::vector<uint8_t> fixed32_bytes(double value) {
  const auto raw_value = static_cast<float>(value);
  uint32_t raw = 0;
  std::memcpy(&raw, &raw_value, sizeof(float));
  return {
      static_cast<uint8_t>(raw & 0xff),
      static_cast<uint8_t>((raw >> 8) & 0xff),
      static_cast<uint8_t>((raw >> 16) & 0xff),
      static_cast<uint8_t>((raw >> 24) & 0xff),
  };
}

std::vector<uint8_t> string_bytes(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::optional<uint64_t> entry_id_from_top9_entry(std::span<const uint8_t> entry) {
  const uint32_t path_value = 501;
  return read_varint_at_path(entry, std::span<const uint32_t>(&path_value, 1));
}

std::vector<Top9Entry> top9_entries(std::span<const uint8_t> top9) {
  std::vector<Field> fields;
  if (!parse_fields(top9, fields)) throw std::runtime_error("top-level field 9 parse failed");

  std::vector<Top9Entry> entries;
  for (const auto& field : fields) {
    if (field.number != 502 || field.wire != 2) continue;
    const auto data = field_data(top9, field);
    const auto entry_id = entry_id_from_top9_entry(data);
    if (!entry_id) continue;
    entries.push_back({*entry_id, std::vector<uint8_t>(data.begin(), data.end())});
  }
  return entries;
}

const Top9Entry* find_entry_by_id(const std::vector<Top9Entry>& entries, uint64_t entry_id) {
  for (const auto& entry : entries) {
    if (entry.entry_id == entry_id) return &entry;
  }
  return nullptr;
}

std::vector<uint64_t> decode_packed_varints(std::span<const uint8_t> data) {
  std::vector<uint64_t> values;
  size_t offset = 0;
  while (offset < data.size()) {
    const auto value = read_varint(data, offset);
    if (!value || value->next <= offset) throw std::runtime_error("invalid packed varint list");
    values.push_back(value->value);
    offset = value->next;
  }
  return values;
}

std::vector<uint8_t> encode_packed_varints(const std::vector<uint64_t>& values) {
  std::vector<uint8_t> out;
  for (uint64_t value : values) {
    const auto encoded = encode_varint(value);
    out.insert(out.end(), encoded.begin(), encoded.end());
  }
  return out;
}

std::optional<std::vector<uint8_t>> direct_len_data(std::span<const uint8_t> message, uint32_t field_number) {
  const auto field = first_len_field(message, field_number);
  if (!field) return std::nullopt;
  const auto data = field_data(message, *field);
  return std::vector<uint8_t>(data.begin(), data.end());
}

std::vector<uint64_t> controller_child_ids(std::span<const uint8_t> top9, uint64_t controller_entry_id) {
  const auto entries = top9_entries(top9);
  const auto* controller = find_entry_by_id(entries, controller_entry_id);
  if (!controller) throw std::runtime_error("ui assets parent not found");
  const auto packed = direct_len_data(bytes_span(controller->data), 503);
  if (!packed) return {};
  return decode_packed_varints(bytes_span(*packed));
}

uint64_t allocate_top9_entry_id(std::span<const uint8_t> top9) {
  uint64_t max_id = 1073741841;
  bool found = false;
  for (const auto& entry : top9_entries(top9)) {
    max_id = found ? std::max(max_id, entry.entry_id) : entry.entry_id;
    found = true;
  }
  return max_id + 1;
}

std::unordered_set<uint64_t> used_entry_ids(std::span<const uint8_t> top9) {
  std::unordered_set<uint64_t> ids;
  for (const auto& entry : top9_entries(top9)) ids.insert(entry.entry_id);
  return ids;
}

std::vector<uint8_t> set_direct_len_field(std::span<const uint8_t> message, uint32_t field_number, std::vector<uint8_t> data) {
  auto fields = parse_owned_fields_or_throw(message, "generated ui asset image message");
  bool changed = false;
  for (auto& field : fields) {
    if (field.number == field_number && field.wire == 2) {
      field.data = std::move(data);
      changed = true;
      break;
    }
  }
  if (!changed) fields.push_back(len_field(field_number, std::move(data)));
  return rebuild_message(fields);
}

std::vector<uint8_t> set_direct_varint_field(std::span<const uint8_t> message, uint32_t field_number, uint64_t value) {
  auto fields = parse_owned_fields_or_throw(message, "ui asset message");
  bool changed = false;
  for (auto& field : fields) {
    if (field.number == field_number && field.wire == 0) {
      field.varint = value;
      changed = true;
      break;
    }
  }
  if (!changed) fields.push_back(varint_field(field_number, value));
  return rebuild_message(fields);
}

void set_fixed32_path(std::vector<OwnedField>& fields, std::span<const uint32_t> path, double value) {
  if (path.empty()) throw std::runtime_error("empty fixed32 path");
  for (auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 5) continue;
      field.data = fixed32_bytes(value);
      return;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "ui asset fixed32 child");
    try {
      set_fixed32_path(child_fields, path.subspan(1), value);
      field.data = rebuild_message(child_fields);
      return;
    } catch (const std::runtime_error&) {
      continue;
    }
  }
  throw std::runtime_error("ui asset fixed32 path not found");
}

std::vector<uint8_t> set_asset_position(std::span<const uint8_t> entry, double x, double y) {
  auto fields = parse_owned_fields_or_throw(entry, "ui asset entry");
  const std::array<uint32_t, 8> pos_x_path{505, 503, 13, 12, 501, 502, 504, 501};
  const std::array<uint32_t, 8> pos_y_path{505, 503, 13, 12, 501, 502, 504, 502};
  set_fixed32_path(fields, pos_x_path, x);
  set_fixed32_path(fields, pos_y_path, y);
  return rebuild_message(fields);
}

std::vector<uint8_t> replace_top9_entry_by_id(
    std::span<const uint8_t> top9,
    uint64_t target_entry_id,
    const std::vector<uint8_t>& new_entry_data) {
  auto fields = parse_owned_fields_or_throw(top9, "top-level field 9");
  bool changed = false;
  for (auto& field : fields) {
    if (field.number != 502 || field.wire != 2) continue;
    const auto entry_id = entry_id_from_top9_entry(bytes_span(field.data));
    if (!entry_id || *entry_id != target_entry_id) continue;
    field.data = new_entry_data;
    changed = true;
    break;
  }
  if (!changed) throw std::runtime_error("top-level field 9 entry not found");
  return rebuild_message(fields);
}

std::vector<uint8_t> set_controller_child_ids(
    std::span<const uint8_t> top9,
    uint64_t controller_entry_id,
    const std::vector<uint64_t>& entry_ids) {
  const auto entries = top9_entries(top9);
  const auto* controller = find_entry_by_id(entries, controller_entry_id);
  if (!controller) throw std::runtime_error("ui assets parent not found");
  const auto next_controller = set_direct_len_field(bytes_span(controller->data), 503, encode_packed_varints(entry_ids));
  return replace_top9_entry_by_id(top9, controller_entry_id, next_controller);
}

std::vector<uint8_t> append_controller_child_ids(
    std::span<const uint8_t> top9,
    uint64_t controller_entry_id,
    const std::vector<uint64_t>& new_entry_ids) {
  auto ids = controller_child_ids(top9, controller_entry_id);
  ids.insert(ids.end(), new_entry_ids.begin(), new_entry_ids.end());
  return set_controller_child_ids(top9, controller_entry_id, ids);
}

std::vector<uint8_t> insert_top9_entries_before(
    std::span<const uint8_t> top9,
    const std::vector<std::vector<uint8_t>>& new_entries,
    uint64_t before_entry_id) {
  auto fields = parse_owned_fields_or_throw(top9, "top-level field 9");
  std::vector<OwnedField> rebuilt;
  rebuilt.reserve(fields.size() + new_entries.size());
  bool inserted = false;

  for (auto& field : fields) {
    if (!inserted && field.number == 502 && field.wire == 2) {
      const auto entry_id = entry_id_from_top9_entry(bytes_span(field.data));
      if (entry_id && *entry_id == before_entry_id) {
        for (const auto& entry : new_entries) rebuilt.push_back(len_field(502, entry));
        inserted = true;
      }
    }
    rebuilt.push_back(std::move(field));
  }

  if (!inserted) {
    for (const auto& entry : new_entries) rebuilt.push_back(len_field(502, entry));
  }
  return rebuild_message(rebuilt);
}

std::vector<uint8_t> vec2(double x, double y) {
  return rebuild_message({
      fixed32_field(501, x),
      fixed32_field(502, y),
  });
}

std::vector<uint8_t> vec3(double x, double y, double z) {
  return rebuild_message({
      fixed32_field(1, x),
      fixed32_field(2, y),
      fixed32_field(3, z),
  });
}

uint64_t raw_color_from_signed(int64_t color) {
  return static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(color)));
}

uint64_t effective_resource_id(const UiAssetImageSpec& image) {
  if (image.primitive_type_id != kUiAssetImageRectResourceId) return image.primitive_type_id;
  return image.resource_id;
}

std::vector<uint8_t> transform_snapshot(const UiAssetImageSpec& primitive) {
  return rebuild_message({
      len_field(501, vec3(primitive.scale_x, primitive.scale_y, primitive.scale_z)),
      len_field(502, vec2(0.5, 0.5)),
      len_field(503, vec2(0.5, 0.5)),
      len_field(504, vec2(primitive.x, primitive.y)),
      len_field(505, vec2(primitive.width, primitive.height)),
      len_field(506, vec2(0.5, 0.5)),
      len_field(508, vec3(0.0, 0.0, primitive.rotation_z)),
  });
}

std::vector<uint8_t> group_transform_snapshot(const UiAssetGroupSpec& group) {
  return rebuild_message({
      len_field(501, vec3(group.scale_x, group.scale_y, group.scale_z)),
      len_field(502, vec2(0.5, 0.5)),
      len_field(503, vec2(0.5, 0.5)),
      len_field(504, vec2(group.x, group.y)),
      len_field(505, vec2(group.width, group.height)),
      len_field(506, vec2(0.5, 0.5)),
      len_field(508, vec3(0.0, 0.0, group.rotation_z)),
  });
}

std::vector<uint8_t> owner_ref(uint64_t entry_id) {
  return rebuild_message({
      varint_field(2, 1),
      varint_field(3, 8),
      varint_field(4, entry_id),
  });
}

std::vector<uint8_t> image_id_component(uint64_t entry_id) {
  return rebuild_message({
      len_field(11, rebuild_message({varint_field(501, entry_id)})),
      varint_field(501, 1),
      varint_field(502, 5),
  });
}

std::vector<uint8_t> image_editor_component(uint64_t entry_id) {
  return rebuild_message({
      len_field(14, rebuild_message({
          len_field(15, {}),
          varint_field(501, 5),
      })),
      varint_field(501, 4),
      varint_field(502, 23),
      len_field(503, rebuild_message({
          len_field(14, rebuild_message({
              len_field(15, {}),
              varint_field(501, 5),
          })),
          varint_field(501, 5),
          varint_field(502, 23),
          varint_field(503, 1),
          len_field(504, owner_ref(entry_id)),
      })),
  });
}

std::vector<uint8_t> named_component(const std::string& name) {
  return rebuild_message({
      len_field(12, rebuild_message({len_field(501, string_bytes(name))})),
      varint_field(501, 2),
      varint_field(502, 15),
  });
}

std::vector<uint8_t> transform_variant(uint64_t variant, const UiAssetImageSpec& image) {
  std::vector<OwnedField> fields;
  if (variant != 0) fields.push_back(varint_field(501, variant));
  fields.push_back(len_field(502, transform_snapshot(image)));
  return rebuild_message(fields);
}

std::vector<uint8_t> transform_component(uint64_t entry_id, const UiAssetImageSpec& image) {
  return rebuild_message({
      len_field(11, rebuild_message({
          len_field(12, {}),
          varint_field(501, 2),
      })),
      varint_field(501, 1),
      varint_field(502, 12),
      len_field(503, rebuild_message({
          len_field(13, rebuild_message({
              len_field(12, rebuild_message({
                  len_field(501, transform_variant(0, image)),
                  len_field(501, transform_variant(1, image)),
                  len_field(501, transform_variant(2, image)),
                  len_field(501, transform_variant(3, image)),
                  varint_field(502, image.layer),
                  varint_field(504, 1),
              })),
              varint_field(501, 2),
          })),
          varint_field(501, 4),
          varint_field(502, 12),
          varint_field(503, 1),
          len_field(504, owner_ref(entry_id)),
      })),
  });
}

std::vector<uint8_t> resource_component(uint64_t entry_id, const UiAssetImageSpec& image) {
  return rebuild_message({
      len_field(31, {}),
      varint_field(501, 21),
      varint_field(502, 38),
      len_field(503, rebuild_message({
          len_field(31, rebuild_message({
              varint_field(2, effective_resource_id(image)),
              len_field(3, rebuild_message({varint_field(501, UINT64_MAX)})),
              varint_field(4, raw_color_from_signed(image.color)),
              len_field(6, {}),
              len_field(10, {}),
          })),
          varint_field(501, 22),
          varint_field(502, 38),
          varint_field(503, 1),
          len_field(504, owner_ref(entry_id)),
      })),
  });
}

std::vector<uint8_t> group_transform_variant(uint64_t variant, const UiAssetGroupSpec& group) {
  std::vector<OwnedField> fields;
  if (variant != 0) fields.push_back(varint_field(501, variant));
  fields.push_back(len_field(502, group_transform_snapshot(group)));
  return rebuild_message(fields);
}

std::vector<uint8_t> group_transform_component(uint64_t entry_id, const UiAssetGroupSpec& group) {
  return rebuild_message({
      len_field(11, rebuild_message({
          len_field(12, {}),
          varint_field(501, 2),
      })),
      varint_field(501, 1),
      varint_field(502, 12),
      len_field(503, rebuild_message({
          len_field(13, rebuild_message({
              len_field(12, rebuild_message({
                  len_field(501, group_transform_variant(0, group)),
                  len_field(501, group_transform_variant(1, group)),
                  len_field(501, group_transform_variant(2, group)),
                  len_field(501, group_transform_variant(3, group)),
                  varint_field(504, 1),
              })),
              varint_field(501, 2),
          })),
          varint_field(501, 4),
          varint_field(502, 12),
          varint_field(503, 1),
          len_field(504, owner_ref(entry_id)),
      })),
  });
}

std::vector<uint8_t> group_kind_component(uint64_t entry_id) {
  return rebuild_message({
      len_field(16, rebuild_message({
          len_field(11, {}),
          varint_field(501, 1),
      })),
      varint_field(501, 6),
      varint_field(502, 55),
      len_field(503, rebuild_message({
          len_field(46, {}),
          varint_field(501, 39),
          varint_field(502, 55),
          varint_field(503, 1),
          len_field(504, owner_ref(entry_id)),
      })),
  });
}

std::vector<uint8_t> group_mask_component(uint64_t entry_id, const UiAssetGroupSpec& group) {
  const double width = group.mask_width.value_or(group.width);
  const double height = group.mask_height.value_or(group.height);
  return rebuild_message({
      len_field(46, {}),
      varint_field(501, 38),
      varint_field(502, 56),
      len_field(503, rebuild_message({
          len_field(47, rebuild_message({
              len_field(1, {}),
              len_field(2, vec2(width, height)),
              varint_field(3, 1),
              varint_field(4, 1),
          })),
          varint_field(501, 40),
          varint_field(502, 56),
          varint_field(503, 1),
          len_field(504, owner_ref(entry_id)),
      })),
  });
}

std::vector<uint8_t> build_group_entry(
    uint64_t entry_id,
    uint64_t parent_entry_id,
    const UiAssetGroupSpec& group,
    const std::vector<uint64_t>& child_entry_ids) {
  return rebuild_message({
      varint_field(501, entry_id),
      len_field(502, group_kind_component(entry_id)),
      len_field(502, image_id_component(entry_id)),
      len_field(503, encode_packed_varints(child_entry_ids)),
      varint_field(504, parent_entry_id),
      len_field(505, named_component(group.name)),
      len_field(505, group_transform_component(entry_id, group)),
      len_field(505, image_editor_component(entry_id)),
      len_field(505, group_mask_component(entry_id, group)),
  });
}

std::vector<uint8_t> build_generated_primitive_entry(
    uint64_t entry_id,
    uint64_t controller_entry_id,
    const UiAssetImageSpec& primitive) {
  return rebuild_message({
      varint_field(501, entry_id),
      len_field(502, image_id_component(entry_id)),
      varint_field(504, controller_entry_id),
      len_field(505, named_component(primitive.name)),
      len_field(505, image_editor_component(entry_id)),
      len_field(505, transform_component(entry_id, primitive)),
      len_field(505, resource_component(entry_id, primitive)),
  });
}

bool is_supported_primitive_type(uint64_t value) {
  return value == kUiAssetImageRectResourceId ||
         value == kUiAssetImageCircleResourceId ||
         value == kUiAssetImageTriangleResourceId;
}

void validate_spec(const UiAssetImageSpec& primitive) {
  if (!is_supported_primitive_type(effective_resource_id(primitive))) {
    throw std::runtime_error("unsupported UI asset image resource id");
  }
  if (primitive.width <= 0.0 || primitive.height <= 0.0) {
    throw std::runtime_error("UI asset image size must be positive");
  }
  if (primitive.scale_x == 0.0 || primitive.scale_y == 0.0 || primitive.scale_z == 0.0) {
    throw std::runtime_error("UI asset image scale must be non-zero");
  }
}

GilFile file_from_bytes(const GilFile& base, const std::vector<uint8_t>& bytes) {
  GilFile file;
  file.path = base.path;
  file.header = base.header;
  file.bytes = bytes;
  return file;
}

UiStructureSummary summary_for_result(
    const GilFile& result_file,
    uint64_t controller_entry_id) {
  const auto list = list_ui_assets(result_file, controller_entry_id);
  UiStructureSummary summary;
  summary.kind = "createUiAssetImages";
  summary.target_controller_entry_id = controller_entry_id;
  summary.primitive_count = list.assets.size();
  summary.changed_top_fields = {9};
  summary.entry_ids.reserve(list.assets.size());
  for (const auto& asset : list.assets) {
    if (asset.entry_id) summary.entry_ids.push_back(*asset.entry_id);
  }
  return summary;
}

}  // namespace

UiStructureMutation create_ui_asset_images(
    const GilFile& file,
    std::span<const UiAssetImageSpec> primitives,
    const UiAssetCreateOptions& options) {
  if (primitives.empty()) throw std::runtime_error("at least one generated UI primitive is required");
  for (const auto& primitive : primitives) validate_spec(primitive);

  const auto top9 = top_level_data(file, 9);
  if (!top9) throw std::runtime_error("top-level field 9 not found");

  const uint64_t controller_id = options.parent_entry_id != kDefaultUiAssetsControllerEntryId
      ? options.parent_entry_id
      : options.target_controller_entry_id;
  controller_child_ids(*top9, controller_id);

  auto used_ids = used_entry_ids(*top9);
  uint64_t next_allocated_id = allocate_top9_entry_id(*top9);
  std::vector<uint64_t> new_entry_ids;
  std::vector<std::vector<uint8_t>> new_entries;
  new_entry_ids.reserve(primitives.size());
  new_entries.reserve(primitives.size());

  for (const auto& primitive : primitives) {
    while (used_ids.contains(next_allocated_id)) ++next_allocated_id;
    const uint64_t entry_id = next_allocated_id++;
    used_ids.insert(entry_id);
    new_entry_ids.push_back(entry_id);
    new_entries.push_back(build_generated_primitive_entry(entry_id, controller_id, primitive));
  }

  auto next_top9 = append_controller_child_ids(*top9, controller_id, new_entry_ids);
  next_top9 = insert_top9_entries_before(bytes_span(next_top9), new_entries, 1073741841);

  auto next_payload = replace_top_level_field_data(payload(file), 9, next_top9);
  auto result_file = file_from_bytes(file, build_gil_bytes(file.header, next_payload));
  auto summary = summary_for_result(result_file, controller_id);

  UiStructureMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

UiStructureMutation create_ui_asset_group(
    const GilFile& file,
    const UiAssetGroupSpec& group,
    const std::vector<size_t>& child_asset_indexes,
    const UiAssetCreateOptions& options) {
  if (child_asset_indexes.empty()) throw std::runtime_error("at least one UI asset child is required");
  if (group.width <= 0.0 || group.height <= 0.0) throw std::runtime_error("UI asset group size must be positive");
  if (group.scale_x == 0.0 || group.scale_y == 0.0 || group.scale_z == 0.0) {
    throw std::runtime_error("UI asset group scale must be non-zero");
  }

  const auto top9 = top_level_data(file, 9);
  if (!top9) throw std::runtime_error("top-level field 9 not found");

  const uint64_t parent_id = options.parent_entry_id != kDefaultUiAssetsControllerEntryId
      ? options.parent_entry_id
      : options.target_controller_entry_id;
  const auto assets = list_ui_assets(file, parent_id);
  const auto entries = top9_entries(*top9);
  auto parent_child_ids = controller_child_ids(*top9, parent_id);

  std::vector<uint64_t> selected_child_ids;
  selected_child_ids.reserve(child_asset_indexes.size());
  std::unordered_set<size_t> seen_indexes;
  for (size_t index : child_asset_indexes) {
    if (index >= assets.assets.size()) throw std::runtime_error("ui asset child index not found");
    if (!seen_indexes.insert(index).second) throw std::runtime_error("duplicate ui asset child index");
    if (!assets.assets[index].entry_id) throw std::runtime_error("ui asset child entry id missing");
    selected_child_ids.push_back(*assets.assets[index].entry_id);
  }
  std::unordered_set<uint64_t> selected_set(selected_child_ids.begin(), selected_child_ids.end());

  auto used_ids = used_entry_ids(*top9);
  uint64_t group_entry_id = allocate_top9_entry_id(*top9);
  while (used_ids.contains(group_entry_id)) ++group_entry_id;

  std::vector<uint64_t> next_parent_child_ids;
  next_parent_child_ids.reserve(parent_child_ids.size() + 1);
  bool inserted_group = false;
  size_t matched = 0;
  for (uint64_t child_id : parent_child_ids) {
    if (selected_set.contains(child_id)) {
      ++matched;
      if (!inserted_group) {
        next_parent_child_ids.push_back(group_entry_id);
        inserted_group = true;
      }
      continue;
    }
    next_parent_child_ids.push_back(child_id);
  }
  if (matched != selected_child_ids.size()) {
    throw std::runtime_error("selected ui asset child is not under parent");
  }

  auto next_top9 = set_controller_child_ids(*top9, parent_id, next_parent_child_ids);
  for (uint64_t child_id : selected_child_ids) {
    const auto* child = find_entry_by_id(entries, child_id);
    if (!child) throw std::runtime_error("selected ui asset entry not found");
    auto next_child = set_direct_varint_field(bytes_span(child->data), 504, group_entry_id);
    for (const auto& asset : assets.assets) {
      if (!asset.entry_id || *asset.entry_id != child_id) continue;
      if (asset.transform.position.x && asset.transform.position.y) {
        next_child = set_asset_position(
            bytes_span(next_child),
            *asset.transform.position.x - group.x,
            *asset.transform.position.y - group.y);
      }
      break;
    }
    next_top9 = replace_top9_entry_by_id(bytes_span(next_top9), child_id, next_child);
  }

  const auto group_entry = build_group_entry(group_entry_id, parent_id, group, selected_child_ids);
  next_top9 = insert_top9_entries_before(bytes_span(next_top9), {group_entry}, selected_child_ids.front());

  auto next_payload = replace_top_level_field_data(payload(file), 9, next_top9);
  auto result_file = file_from_bytes(file, build_gil_bytes(file.header, next_payload));
  auto summary = summary_for_result(result_file, parent_id);
  summary.kind = "createUiAssetGroup";

  UiStructureMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

UiStructureMutation append_generated_ui_primitives(
    const GilFile& file,
    std::span<const UiGeneratedPrimitiveSpec> primitives,
    const UiGeneratedPrimitiveOptions& options) {
  return create_ui_asset_images(file, primitives, options);
}

}  // namespace opengil
