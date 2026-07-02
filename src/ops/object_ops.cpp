#include "opengil/object_ops.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "opengil/model_ops.hpp"
#include "opengil/semantic.hpp"

namespace opengil {
namespace {

OwnedField make_varint_field(uint32_t number, uint64_t value) {
  OwnedField field;
  field.number = number;
  field.wire = 0;
  field.varint = value;
  return field;
}

OwnedField make_len_field(uint32_t number, std::vector<uint8_t> data) {
  OwnedField field;
  field.number = number;
  field.wire = 2;
  field.data = std::move(data);
  return field;
}

OwnedField make_fixed32_field(uint32_t number, float value) {
  uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof(float));
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

template <size_t N>
std::optional<uint64_t> read_varint_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_varint_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

bool replace_varint_at_path(std::vector<OwnedField>& fields, std::span<const uint32_t> path, uint64_t value) {
  if (path.empty()) return false;
  for (auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 0) continue;
      field.varint = value;
      return true;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "nested varint child");
    if (replace_varint_at_path(child_fields, path.subspan(1), value)) {
      field.data = rebuild_message(child_fields);
      return true;
    }
  }
  return false;
}

bool replace_len_data_at_path(
    std::vector<OwnedField>& fields,
    std::span<const uint32_t> path,
    const std::vector<uint8_t>& data) {
  if (path.empty()) return false;
  for (auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 2) continue;
      field.data = data;
      return true;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "nested len child");
    if (replace_len_data_at_path(child_fields, path.subspan(1), data)) {
      field.data = rebuild_message(child_fields);
      return true;
    }
  }
  return false;
}

std::vector<uint8_t> string_bytes(std::string_view value) {
  return std::vector<uint8_t>(value.begin(), value.end());
}

uint8_t hex_nibble(char value) {
  if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
  if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
  throw std::runtime_error("invalid hex literal");
}

std::vector<uint8_t> hex_bytes(std::string_view hex) {
  if (hex.size() % 2 != 0) throw std::runtime_error("invalid hex literal length");
  std::vector<uint8_t> out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    out.push_back(static_cast<uint8_t>((hex_nibble(hex[i]) << 4) | hex_nibble(hex[i + 1])));
  }
  return out;
}

std::vector<uint8_t> editor_empty_prefab_template_entry() {
  return hex_bytes(
      "088380808204109ad4e204321208015a0e0a0c656d7074795f707265666162"
      "320b080db2010620ffffffff0f3216080eba01110a0f1a0d4d50416374696f6e47726f7570"
      "320a08268203050d0000803f320508289203003205086fea05003205083d8a04003205083e920400"
      "3a2a08015a260a0a0da891c8401d5b49ccc012001a0f0d0000803f150000803f1d0000803fa81fffffffff0f"
      "3a04080262003a0408036a003a060804720208013a0808057a04080110013a050806820100"
      "3a4208078a013d0d00007a441d0000fa4320012801320510c2c7ee0445cdcccc3d4dcdcccc3d"
      "55cdcccc3d5dcdcccc3d65cdcccc3d6dcdcccc3d75cdcccc3d7dcdcccc3d"
      "3a0a08089201050801a81f013a36080baa01310a2f0a0b47495f526f6f744e6f646512001a00"
      "b21f0d43656e746572204f726967696ec01f01ca1f08526f6f744e6f6465"
      "3a08080cb20103a81f013a050810d201003a050811da01003a070813ea01020801"
      "3a050814f201003a18081682021318ffffffff0f250000c84228ffffff0730ac34"
      "426408121001e2015d4a23180120012a0032003d0000803f420052005801"
      "ba1f0a48697420456666656374d81f0d"
      "5229180120012a0032003d0000803f420052005801ba1f104b6e6f636b646f776e20456666656374d81f0d"
      "5a0b47495f526f6f744e6f6465"
      "4206080110015a004206080310016a00420708131001ea01004207080610018201004207080e1001c201005001");
}

bool replace_string_at_path(
    std::vector<OwnedField>& fields,
    std::span<const uint32_t> path,
    std::string_view value) {
  return replace_len_data_at_path(fields, path, string_bytes(value));
}

bool replace_prefab_name(std::vector<OwnedField>& entry_fields, std::string_view new_name) {
  const std::array<uint32_t, 3> preferred{6, 11, 1};
  if (replace_string_at_path(entry_fields, std::span<const uint32_t>(preferred.data(), preferred.size()), new_name)) {
    return true;
  }
  const std::array<uint32_t, 2> fallback1{6, 11};
  if (replace_string_at_path(entry_fields, std::span<const uint32_t>(fallback1.data(), fallback1.size()), new_name)) {
    return true;
  }
  const std::array<uint32_t, 1> fallback2{3};
  return replace_string_at_path(entry_fields, std::span<const uint32_t>(fallback2.data(), fallback2.size()), new_name);
}

std::vector<uint8_t> replace_prefab_entry_name(std::span<const uint8_t> entry, std::string_view new_name) {
  auto fields = parse_owned_fields_or_throw(entry, "prefab create rename");
  if (!replace_prefab_name(fields, new_name)) {
    throw std::runtime_error("prefab name path not found");
  }
  return rebuild_message(fields);
}

std::vector<uint8_t> replace_nested_varint(
    std::span<const uint8_t> message,
    std::span<const uint32_t> path,
    uint64_t value) {
  auto fields = parse_owned_fields_or_throw(message, "nested varint message");
  if (!replace_varint_at_path(fields, path, value)) {
    throw std::runtime_error("nested varint path not found");
  }
  return rebuild_message(fields);
}

std::vector<uint8_t> build_vec3_message(const Vec3& value) {
  return rebuild_message({
      make_fixed32_field(1, static_cast<float>(value.x)),
      make_fixed32_field(2, static_cast<float>(value.y)),
      make_fixed32_field(3, static_cast<float>(value.z)),
  });
}

std::vector<uint8_t> build_transform_message(const Transform& transform) {
  return rebuild_message({
      make_len_field(1, build_vec3_message(transform.position)),
      make_len_field(2, build_vec3_message(transform.rotation)),
      make_len_field(3, build_vec3_message(transform.scale)),
      make_varint_field(501, 0xffffffffull),
  });
}

uint64_t raw_color_from_signed(int64_t color) {
  return static_cast<uint64_t>(static_cast<uint32_t>(color));
}

int64_t signed_color_from_raw(uint64_t raw) {
  return static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(raw)));
}

uint64_t rgb_color_from_signed(int64_t color) {
  return raw_color_from_signed(color) & 0x00ffffffull;
}

bool set_varint_field(std::vector<OwnedField>& fields, uint32_t number, uint64_t value) {
  for (auto& field : fields) {
    if (field.number != number || field.wire != 0) continue;
    field.varint = value;
    return true;
  }
  return false;
}

void upsert_varint_field(std::vector<OwnedField>& fields, uint32_t number, uint64_t value) {
  if (!set_varint_field(fields, number, value)) fields.push_back(make_varint_field(number, value));
}

bool set_fixed32_field(std::vector<OwnedField>& fields, uint32_t number, float value) {
  for (auto& field : fields) {
    if (field.number != number || field.wire != 5) continue;
    field = make_fixed32_field(number, value);
    return true;
  }
  return false;
}

void upsert_fixed32_field(std::vector<OwnedField>& fields, uint32_t number, float value) {
  if (!set_fixed32_field(fields, number, value)) fields.push_back(make_fixed32_field(number, value));
}

bool set_len_field(std::vector<OwnedField>& fields, uint32_t number, const std::vector<uint8_t>& data) {
  for (auto& field : fields) {
    if (field.number != number || field.wire != 2) continue;
    field.data = data;
    return true;
  }
  return false;
}

void upsert_len_field(std::vector<OwnedField>& fields, uint32_t number, std::vector<uint8_t> data) {
  if (!set_len_field(fields, number, data)) fields.push_back(make_len_field(number, std::move(data)));
}

struct SceneObjectColorState {
  std::optional<int64_t> color;
  std::optional<uint64_t> raw_color;
  std::optional<uint64_t> rgb_color;
  std::optional<bool> enabled;
};

SceneObjectColorState read_color_payload(std::span<const uint8_t> payload) {
  const std::array<uint32_t, 1> enabled_path{1};
  const std::array<uint32_t, 1> color_path{3};
  const std::array<uint32_t, 1> rgb_path{5};

  SceneObjectColorState state;
  if (auto enabled = read_varint_path(payload, enabled_path)) state.enabled = *enabled != 0;
  else state.enabled = false;
  state.raw_color = read_varint_path(payload, color_path);
  if (state.raw_color) state.color = signed_color_from_raw(*state.raw_color);
  state.rgb_color = read_varint_path(payload, rgb_path);
  return state;
}

std::optional<SceneObjectColorState> read_scene_object_color(std::span<const uint8_t> entry) {
  const std::array<uint32_t, 1> component_id_path{1};
  for (const auto& component_field : len_fields(entry, 6)) {
    const auto component = field_data(entry, component_field);
    if (read_varint_path(component, component_id_path) != 22) continue;
    const auto payload_field = first_len_field(component, 32);
    if (!payload_field) continue;
    return read_color_payload(field_data(component, *payload_field));
  }
  return std::nullopt;
}

std::vector<uint8_t> build_model_color_payload(int64_t color) {
  return rebuild_message({
      make_varint_field(1, 1),
      make_varint_field(3, raw_color_from_signed(color)),
      make_fixed32_field(4, 100.0f),
      make_varint_field(5, rgb_color_from_signed(color)),
      make_varint_field(6, 6700),
      make_varint_field(9, 6711),
  });
}

std::vector<uint8_t> upsert_model_color_payload(std::span<const uint8_t> payload, int64_t color) {
  auto fields = parse_owned_fields_or_throw(payload, "model color payload");
  upsert_varint_field(fields, 1, 1);
  upsert_varint_field(fields, 3, raw_color_from_signed(color));
  upsert_fixed32_field(fields, 4, 100.0f);
  upsert_varint_field(fields, 5, rgb_color_from_signed(color));
  upsert_varint_field(fields, 6, 6700);
  upsert_varint_field(fields, 9, 6711);
  return rebuild_message(fields);
}

std::vector<uint8_t> build_model_color_component(int64_t color) {
  return rebuild_message({
      make_varint_field(1, 22),
      make_len_field(32, build_model_color_payload(color)),
  });
}

std::vector<uint8_t> upsert_model_color_component(std::span<const uint8_t> component, int64_t color) {
  auto fields = parse_owned_fields_or_throw(component, "model color component");
  upsert_varint_field(fields, 1, 22);

  std::optional<std::vector<uint8_t>> color_payload;
  for (const auto& field : fields) {
    if (field.number == 32 && field.wire == 2) {
      color_payload = field.data;
      break;
    }
  }

  const auto next_payload = color_payload
      ? upsert_model_color_payload(std::span<const uint8_t>(color_payload->data(), color_payload->size()), color)
      : build_model_color_payload(color);
  upsert_len_field(fields, 32, next_payload);
  return rebuild_message(fields);
}

std::vector<uint8_t> upsert_scene_color_component(std::span<const uint8_t> entry, int64_t color) {
  auto fields = parse_owned_fields_or_throw(entry, "scene color entry");
  const std::array<uint32_t, 1> component_id_path{1};
  for (auto& field : fields) {
    if (field.number != 6 || field.wire != 2) continue;
    const std::span<const uint8_t> component(field.data.data(), field.data.size());
    if (read_varint_path(component, component_id_path) != 22) continue;
    field.data = upsert_model_color_component(component, color);
    return rebuild_message(fields);
  }

  fields.push_back(make_len_field(6, build_model_color_component(color)));
  return rebuild_message(fields);
}

std::vector<uint8_t> replace_entry_transform(
    std::span<const uint8_t> entry,
    uint32_t transform_field,
    const Transform& transform) {
  auto fields = parse_owned_fields_or_throw(entry, "transform entry");
  const std::array<uint32_t, 2> transform_path{transform_field, 11};
  if (!replace_len_data_at_path(fields, std::span<const uint32_t>(transform_path.data(), transform_path.size()), build_transform_message(transform))) {
    throw std::runtime_error("transform path not found");
  }
  return rebuild_message(fields);
}

std::optional<std::vector<uint8_t>> top_level_data_from_payload(
    std::span<const uint8_t> payload_bytes,
    uint32_t field_number) {
  std::vector<Field> parsed;
  if (!parse_fields(payload_bytes, parsed)) return std::nullopt;
  for (const auto& field : parsed) {
    if (field.number != field_number || field.wire != 2) continue;
    const auto data = field_data(payload_bytes, field);
    return std::vector<uint8_t>(data.begin(), data.end());
  }
  return std::nullopt;
}

std::vector<uint64_t> collect_ids_from_top_field(std::span<const uint8_t> payload_bytes, uint32_t top_field_number) {
  std::vector<uint64_t> ids;
  const auto top = top_level_data_from_payload(payload_bytes, top_field_number);
  if (!top) return ids;
  for (const auto& field : len_fields(*top, 1)) {
    const auto entry = field_data(*top, field);
    const std::array<uint32_t, 1> id_path{1};
    if (auto id = read_varint_path(entry, id_path)) ids.push_back(*id);
  }
  return ids;
}

uint64_t allocate_object_id(std::span<const uint8_t> payload_bytes) {
  uint64_t max_id = 1077936128;
  bool saw = false;
  for (uint32_t top_field : {4u, 5u, 8u}) {
    for (uint64_t id : collect_ids_from_top_field(payload_bytes, top_field)) {
      max_id = std::max(max_id, id);
      saw = true;
    }
  }
  return saw ? max_id + 1 : 1077936129;
}

bool object_id_exists(std::span<const uint8_t> payload_bytes, uint64_t object_id) {
  for (uint32_t top_field : {4u, 5u, 8u}) {
    for (uint64_t existing_id : collect_ids_from_top_field(payload_bytes, top_field)) {
      if (existing_id == object_id) return true;
    }
  }
  return false;
}

uint64_t resolve_create_id(
    std::span<const uint8_t> payload_bytes,
    const std::optional<uint64_t>& requested_id,
    std::string_view label) {
  if (!requested_id) return allocate_object_id(payload_bytes);
  if (object_id_exists(payload_bytes, *requested_id)) {
    throw std::runtime_error(std::string(label) + " id already exists");
  }
  return *requested_id;
}

std::vector<uint8_t> append_repeated_entry(
    std::span<const uint8_t> top_data,
    uint32_t repeated_field,
    std::vector<uint8_t> entry) {
  auto fields = parse_owned_fields_or_throw(top_data, "append repeated entry");
  fields.push_back(make_len_field(repeated_field, std::move(entry)));
  return rebuild_message(fields);
}

std::vector<uint8_t> find_template_entry(
    std::span<const uint8_t> top_data,
    const std::vector<uint64_t>& preferred_ref_ids) {
  std::vector<std::vector<uint8_t>> entries;
  for (const auto& field : len_fields(top_data, 1)) {
    const auto entry = field_data(top_data, field);
    entries.emplace_back(entry.begin(), entry.end());
  }

  const std::array<uint32_t, 2> prefab_ref_path{2, 1};
  const std::array<uint32_t, 1> direct_ref_path{2};
  for (uint64_t ref_id : preferred_ref_ids) {
    for (const auto& entry : entries) {
      const std::span<const uint8_t> entry_span(entry.data(), entry.size());
      if (read_varint_path(entry_span, prefab_ref_path) == ref_id ||
          read_varint_path(entry_span, direct_ref_path) == ref_id) {
        return entry;
      }
    }
  }

  const std::array<uint32_t, 1> id_path{1};
  for (const auto& entry : entries) {
    if (read_varint_path(std::span<const uint8_t>(entry.data(), entry.size()), id_path)) return entry;
  }
  throw std::runtime_error("no reusable entry template found");
}

std::optional<std::vector<uint8_t>> find_prefab_template_entry(
    const GilFile* template_file,
    uint64_t asset_id) {
  if (!template_file) return std::nullopt;
  const auto top4 = top_level_data(*template_file, 4);
  if (!top4) return std::nullopt;
  const std::array<uint32_t, 1> asset_path{2};
  for (const auto& field : len_fields(*top4, 1)) {
    const auto entry = field_data(*top4, field);
    if (read_varint_path(entry, asset_path) == asset_id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  return std::nullopt;
}

std::optional<std::vector<uint8_t>> find_scene_prefab_instance_template_entry(
    const GilFile* template_file,
    uint64_t asset_id) {
  if (!template_file) return std::nullopt;
  const auto top5 = top_level_data(*template_file, 5);
  if (!top5) return std::nullopt;
  const std::array<uint32_t, 1> asset_path{8};
  const std::array<uint32_t, 2> ref_path{2, 1};
  for (const auto& field : len_fields(*top5, 1)) {
    const auto entry = field_data(*top5, field);
    const auto asset = read_varint_path(entry, asset_path);
    const auto ref = read_varint_path(entry, ref_path);
    if (asset == asset_id && ref && *ref != asset_id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  return std::nullopt;
}

std::vector<uint8_t> build_mapping(uint32_t mapping_type, uint64_t target_id) {
  return rebuild_message({
      make_varint_field(1, mapping_type),
      make_varint_field(2, target_id),
  });
}

std::vector<uint8_t> append_mapping_to_category_id(
    std::span<const uint8_t> top6,
    uint64_t category_id,
    uint32_t mapping_type,
    uint64_t target_id) {
  auto fields = parse_owned_fields_or_throw(top6, "top6 category mappings");
  bool changed = false;
  for (auto& field : fields) {
    if (changed || field.number != 1 || field.wire != 2) continue;
    const std::array<uint32_t, 1> category_path{1};
    if (read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), category_path) != category_id) continue;

    auto entry_fields = parse_owned_fields_or_throw(field.data, "top6 category entry");
    for (auto& entry_field : entry_fields) {
      if (changed || entry_field.number != 3 || entry_field.wire != 2) continue;
      auto category_fields = parse_owned_fields_or_throw(entry_field.data, "top6 category child");
      category_fields.push_back(make_len_field(5, build_mapping(mapping_type, target_id)));
      entry_field.data = rebuild_message(category_fields);
      field.data = rebuild_message(entry_fields);
      changed = true;
    }
  }
  if (!changed) throw std::runtime_error("category id not found in top6");
  return rebuild_message(fields);
}

std::vector<uint8_t> patch_top6_mapping(
    std::span<const uint8_t> payload_bytes,
    uint32_t mapping_type,
    uint64_t target_id,
    const std::vector<uint64_t>& category_ids,
    std::vector<uint32_t>& changed_top_fields) {
  const auto top6 = top_level_data_from_payload(payload_bytes, 6);
  if (!top6) return std::vector<uint8_t>(payload_bytes.begin(), payload_bytes.end());

  auto next_top6 = *top6;
  for (uint64_t category_id : category_ids) {
    next_top6 = append_mapping_to_category_id(next_top6, category_id, mapping_type, target_id);
  }
  changed_top_fields.push_back(6);
  return replace_top_level_field_data(payload_bytes, 6, next_top6);
}

ObjectMutation set_space_transform(
    const GilFile& file,
    uint32_t top_field_number,
    uint32_t transform_field_number,
    std::string kind,
    uint64_t object_id,
    const Transform& transform) {
  const auto top = top_level_data(file, top_field_number);
  if (!top) throw std::runtime_error("top-level field not found");

  auto fields = parse_owned_fields_or_throw(*top, "object space top-level field");
  bool changed = false;
  for (auto& field : fields) {
    if (changed || field.number != 1 || field.wire != 2) continue;
    const std::span<const uint8_t> entry(field.data.data(), field.data.size());
    const std::array<uint32_t, 1> id_path{1};
    if (read_varint_path(entry, id_path) != object_id) continue;
    field.data = replace_entry_transform(entry, transform_field_number, transform);
    changed = true;
  }
  if (!changed) throw std::runtime_error("object id not found");

  auto next_payload = replace_top_level_field_data(payload(file), top_field_number, rebuild_message(fields));
  ObjectSummary summary;
  summary.kind = std::move(kind);
  summary.object_id = object_id;
  summary.transform = transform;
  summary.changed_top_fields.push_back(top_field_number);

  ObjectMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = summary;
  mutation.changed_top_fields = std::move(summary.changed_top_fields);
  return mutation;
}

std::vector<uint8_t> replace_scene_asset_id(std::span<const uint8_t> entry, uint64_t asset_id) {
  auto fields = parse_owned_fields_or_throw(entry, "scene object asset entry");
  const std::array<uint32_t, 1> asset_path{8};
  const std::array<uint32_t, 2> ref_path{2, 1};
  const auto previous_asset_id = read_varint_path(entry, asset_path);
  if (!replace_varint_at_path(fields, std::span<const uint32_t>(asset_path.data(), asset_path.size()), asset_id)) {
    throw std::runtime_error("scene object asset id path not found");
  }

  const auto ref_id = read_varint_path(entry, ref_path);
  if (previous_asset_id && ref_id == previous_asset_id) {
    replace_varint_at_path(fields, std::span<const uint32_t>(ref_path.data(), ref_path.size()), asset_id);
  }

  return rebuild_message(fields);
}

ObjectMutation make_object_mutation(
    const GilFile& file,
    std::vector<uint8_t> next_payload,
    ObjectSummary summary,
    std::vector<uint32_t> changed_top_fields) {
  ObjectMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  mutation.changed_top_fields = std::move(changed_top_fields);
  return mutation;
}

ObjectSummary create_scene_object_summary(
    std::string kind,
    uint64_t object_id,
    uint64_t asset_id,
    const Transform& transform,
    const std::vector<uint32_t>& changed_top_fields) {
  ObjectSummary summary;
  summary.kind = std::move(kind);
  summary.object_id = object_id;
  summary.asset_id = asset_id;
  summary.transform = transform;
  summary.changed_top_fields = changed_top_fields;
  return summary;
}

ObjectSummary create_prefab_summary(
    uint64_t prefab_id,
    uint64_t asset_id,
    const Transform& transform,
    const std::vector<uint32_t>& changed_top_fields) {
  ObjectSummary summary;
  summary.kind = "prefab";
  summary.prefab_id = prefab_id;
  summary.asset_id = asset_id;
  summary.transform = transform;
  summary.changed_top_fields = changed_top_fields;
  return summary;
}

ObjectSummary create_scene_prefab_instance_summary(
    uint64_t object_id,
    uint64_t prefab_id,
    uint64_t asset_id,
    const Transform& transform,
    const std::vector<uint32_t>& changed_top_fields) {
  ObjectSummary summary;
  summary.kind = "scenePrefabInstance";
  summary.object_id = object_id;
  summary.prefab_id = prefab_id;
  summary.asset_id = asset_id;
  summary.transform = transform;
  summary.changed_top_fields = changed_top_fields;
  return summary;
}

std::vector<uint8_t> find_prefab_entry(std::span<const uint8_t> top4, uint64_t prefab_id) {
  const std::array<uint32_t, 1> id_path{1};
  for (const auto& field : len_fields(top4, 1)) {
    const auto entry = field_data(top4, field);
    if (read_varint_path(entry, id_path) == prefab_id) return std::vector<uint8_t>(entry.begin(), entry.end());
  }
  throw std::runtime_error("prefab id not found");
}

std::vector<uint8_t> build_prefab_preview_entry(
    std::span<const uint8_t> prefab_entry,
    uint64_t object_id,
    uint64_t prefab_id,
    const Transform& transform) {
  const std::array<uint32_t, 1> asset_path{2};
  const auto asset_id = read_varint_path(prefab_entry, asset_path);
  if (!asset_id) throw std::runtime_error("prefab asset id not found");

  std::vector<OwnedField> fields;
  fields.push_back(make_varint_field(1, object_id));
  fields.push_back(make_len_field(2, rebuild_message({make_varint_field(1, prefab_id)})));

  for (const auto& field : parse_owned_fields_or_throw(prefab_entry, "prefab preview source")) {
    if (field.number == 6 && field.wire == 2) fields.push_back(make_len_field(5, field.data));
  }
  fields.push_back(make_len_field(5, rebuild_message({make_varint_field(1, 19), make_len_field(28, {})})));
  fields.push_back(make_len_field(5, rebuild_message({make_varint_field(1, 20), make_len_field(29, {})})));
  fields.push_back(make_len_field(5, rebuild_message({make_varint_field(1, 52), make_len_field(62, {})})));

  for (const auto& field : parse_owned_fields_or_throw(prefab_entry, "prefab preview source")) {
    if (field.number == 7 && field.wire == 2) fields.push_back(make_len_field(6, field.data));
    if (field.number == 8 && field.wire == 2) fields.push_back(make_len_field(7, field.data));
  }
  fields.push_back(make_varint_field(8, *asset_id));

  return replace_entry_transform(rebuild_message(fields), 6, transform);
}

}  // namespace

ObjectMutation create_scene_object(const GilFile& file, uint64_t asset_id, const CreateSceneObjectOptions& options) {
  const auto top5 = top_level_data(file, 5);
  if (!top5) throw std::runtime_error("top-level field 5 not found");

  const uint64_t object_id = resolve_create_id(payload(file), options.object_id, "object");
  auto entry = find_template_entry(*top5, {asset_id, 10003004});
  const std::array<uint32_t, 1> id_path{1};
  const std::array<uint32_t, 2> ref_path{2, 1};
  const std::array<uint32_t, 1> asset_path{8};
  entry = replace_nested_varint(entry, id_path, object_id);
  entry = replace_nested_varint(entry, ref_path, asset_id);
  entry = replace_nested_varint(entry, asset_path, asset_id);
  entry = replace_entry_transform(entry, 6, options.transform);

  std::vector<uint32_t> changed_top_fields{5};
  auto next_payload = replace_top_level_field_data(payload(file), 5, append_repeated_entry(*top5, 1, std::move(entry)));
  next_payload = patch_top6_mapping(next_payload, 200, object_id, {3}, changed_top_fields);

  auto summary = create_scene_object_summary("sceneObject", object_id, asset_id, options.transform, changed_top_fields);
  return make_object_mutation(file, std::move(next_payload), std::move(summary), std::move(changed_top_fields));
}

ObjectMutation create_prefab(
    const GilFile& file,
    uint64_t asset_id,
    const CreatePrefabOptions& options,
    const GilFile* template_file) {
  const auto top4 = top_level_data(file, 4);
  if (!top4) throw std::runtime_error("top-level field 4 not found");

  const uint64_t prefab_id = resolve_create_id(payload(file), options.prefab_id, "prefab");
  auto template_entry = find_prefab_template_entry(template_file, asset_id);
  auto entry = template_entry
      ? std::move(*template_entry)
      : (asset_id == EMPTY_MODEL_ASSET_ID ? editor_empty_prefab_template_entry() : find_template_entry(*top4, {asset_id, 1000000}));
  const std::array<uint32_t, 1> id_path{1};
  const std::array<uint32_t, 1> asset_path{2};
  entry = replace_nested_varint(entry, id_path, prefab_id);
  entry = replace_nested_varint(entry, asset_path, asset_id);
  if (options.name && !options.name->empty()) {
    entry = replace_prefab_entry_name(entry, *options.name);
  }
  entry = replace_entry_transform(entry, 7, options.transform);

  std::vector<uint32_t> changed_top_fields{4};
  auto next_payload = replace_top_level_field_data(payload(file), 4, append_repeated_entry(*top4, 1, std::move(entry)));
  next_payload = patch_top6_mapping(next_payload, 100, prefab_id, {6, 3}, changed_top_fields);

  auto summary = create_prefab_summary(prefab_id, asset_id, options.transform, changed_top_fields);
  return make_object_mutation(file, std::move(next_payload), std::move(summary), std::move(changed_top_fields));
}

ObjectMutation create_scene_prefab_instance(
    const GilFile& file,
    uint64_t prefab_id,
    uint64_t asset_id,
    const CreateScenePrefabInstanceOptions& options,
    const GilFile* template_file) {
  const auto top5 = top_level_data(file, 5);
  if (!top5) throw std::runtime_error("top-level field 5 not found");

  const uint64_t object_id = resolve_create_id(payload(file), options.object_id, "object");
  auto template_entry = find_scene_prefab_instance_template_entry(template_file, asset_id);
  auto entry = template_entry ? std::move(*template_entry) : find_template_entry(*top5, {prefab_id, asset_id, 10003004});
  const std::array<uint32_t, 1> id_path{1};
  const std::array<uint32_t, 2> ref_path{2, 1};
  const std::array<uint32_t, 1> asset_path{8};
  entry = replace_nested_varint(entry, id_path, object_id);
  entry = replace_nested_varint(entry, ref_path, prefab_id);
  entry = replace_nested_varint(entry, asset_path, asset_id);
  entry = replace_entry_transform(entry, 6, options.transform);

  std::vector<uint32_t> changed_top_fields{5};
  auto next_payload = replace_top_level_field_data(payload(file), 5, append_repeated_entry(*top5, 1, std::move(entry)));
  next_payload = patch_top6_mapping(next_payload, 200, object_id, {3}, changed_top_fields);

  auto summary = create_scene_prefab_instance_summary(
      object_id,
      prefab_id,
      asset_id,
      options.transform,
      changed_top_fields);
  return make_object_mutation(file, std::move(next_payload), std::move(summary), std::move(changed_top_fields));
}

ObjectMutation create_prefab_preview(
    const GilFile& file,
    uint64_t prefab_id,
    const CreatePrefabPreviewOptions& options) {
  const auto top4 = top_level_data(file, 4);
  const auto top8 = top_level_data(file, 8);
  if (!top4 || !top8) throw std::runtime_error("top-level fields 4 and 8 are required");

  const uint64_t object_id = resolve_create_id(payload(file), options.object_id, "object");
  const auto prefab_entry = find_prefab_entry(*top4, prefab_id);
  const auto preview_entry = build_prefab_preview_entry(prefab_entry, object_id, prefab_id, options.transform);

  std::vector<uint32_t> changed_top_fields{8};
  auto next_payload = replace_top_level_field_data(payload(file), 8, append_repeated_entry(*top8, 1, preview_entry));
  next_payload = patch_top6_mapping(next_payload, 400, object_id, {3}, changed_top_fields);

  const std::array<uint32_t, 1> asset_path{2};
  auto summary = create_scene_prefab_instance_summary(
      object_id,
      prefab_id,
      read_varint_path(prefab_entry, asset_path).value_or(0),
      options.transform,
      changed_top_fields);
  summary.kind = "prefabPreview";
  return make_object_mutation(file, std::move(next_payload), std::move(summary), std::move(changed_top_fields));
}

ObjectMutation set_scene_transform(const GilFile& file, uint64_t object_id, const Transform& transform) {
  return set_space_transform(file, 5, 6, "sceneTransform", object_id, transform);
}

ObjectMutation set_preview_transform(const GilFile& file, uint64_t object_id, const Transform& transform) {
  return set_space_transform(file, 8, 6, "previewTransform", object_id, transform);
}

ObjectMutation set_scene_object_asset_id(const GilFile& file, uint64_t object_id, uint64_t asset_id) {
  const auto top5 = top_level_data(file, 5);
  if (!top5) throw std::runtime_error("top-level field 5 not found");

  auto fields = parse_owned_fields_or_throw(*top5, "scene object top-level field");
  bool changed = false;
  for (auto& field : fields) {
    if (changed || field.number != 1 || field.wire != 2) continue;
    const std::span<const uint8_t> entry(field.data.data(), field.data.size());
    const std::array<uint32_t, 1> id_path{1};
    if (read_varint_path(entry, id_path) != object_id) continue;
    field.data = replace_scene_asset_id(entry, asset_id);
    changed = true;
  }
  if (!changed) throw std::runtime_error("object id not found");

  std::vector<uint32_t> changed_top_fields{5};
  auto next_payload = replace_top_level_field_data(payload(file), 5, rebuild_message(fields));

  ObjectSummary summary;
  summary.kind = "sceneObjectAsset";
  summary.object_id = object_id;
  summary.asset_id = asset_id;
  summary.changed_top_fields = changed_top_fields;
  return make_object_mutation(file, std::move(next_payload), std::move(summary), std::move(changed_top_fields));
}

ObjectColorMutation set_scene_object_color(const GilFile& file, uint64_t object_id, int64_t color) {
  const auto top5 = top_level_data(file, 5);
  if (!top5) throw std::runtime_error("top-level field 5 not found");

  auto fields = parse_owned_fields_or_throw(*top5, "scene object top-level field");
  bool changed = false;
  std::optional<SceneObjectColorState> before;
  for (auto& field : fields) {
    if (changed || field.number != 1 || field.wire != 2) continue;
    const std::span<const uint8_t> entry(field.data.data(), field.data.size());
    const std::array<uint32_t, 1> id_path{1};
    if (read_varint_path(entry, id_path) != object_id) continue;
    before = read_scene_object_color(entry);
    field.data = upsert_scene_color_component(entry, color);
    changed = true;
  }
  if (!changed) throw std::runtime_error("object id not found");

  std::vector<uint32_t> changed_top_fields{5};
  auto next_payload = replace_top_level_field_data(payload(file), 5, rebuild_message(fields));

  ObjectColorSummary summary;
  summary.kind = "sceneObjectColor";
  summary.object_id = object_id;
  if (before) {
    summary.before_color = before->color;
    summary.before_raw_color = before->raw_color;
    summary.before_rgb_color = before->rgb_color;
    summary.before_enabled = before->enabled;
  }
  summary.after_color = color;
  summary.after_raw_color = raw_color_from_signed(color);
  summary.after_rgb_color = rgb_color_from_signed(color);
  summary.after_enabled = true;
  summary.changed_top_fields = changed_top_fields;

  ObjectColorMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  mutation.changed_top_fields = std::move(changed_top_fields);
  return mutation;
}

}  // namespace opengil
