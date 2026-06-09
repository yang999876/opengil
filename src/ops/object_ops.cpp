#include "opengil/object_ops.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "opengil/json.hpp"
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
    auto child_fields = parse_owned_fields(field.data);
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
    auto child_fields = parse_owned_fields(field.data);
    if (replace_len_data_at_path(child_fields, path.subspan(1), data)) {
      field.data = rebuild_message(child_fields);
      return true;
    }
  }
  return false;
}

std::vector<uint8_t> replace_nested_varint(
    std::span<const uint8_t> message,
    std::span<const uint32_t> path,
    uint64_t value) {
  auto fields = parse_owned_fields(message);
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

std::vector<uint8_t> replace_entry_transform(
    std::span<const uint8_t> entry,
    uint32_t transform_field,
    const Transform& transform) {
  auto fields = parse_owned_fields(entry);
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

std::vector<uint8_t> append_repeated_entry(
    std::span<const uint8_t> top_data,
    uint32_t repeated_field,
    std::vector<uint8_t> entry) {
  auto fields = parse_owned_fields(top_data);
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
  auto fields = parse_owned_fields(top6);
  bool changed = false;
  for (auto& field : fields) {
    if (changed || field.number != 1 || field.wire != 2) continue;
    const std::array<uint32_t, 1> category_path{1};
    if (read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), category_path) != category_id) continue;

    auto entry_fields = parse_owned_fields(field.data);
    for (auto& entry_field : entry_fields) {
      if (changed || entry_field.number != 3 || entry_field.wire != 2) continue;
      auto category_fields = parse_owned_fields(entry_field.data);
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

  auto fields = parse_owned_fields(*top);
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
  TransformSummary summary;
  summary.kind = std::move(kind);
  summary.object_id = object_id;
  summary.transform = transform;
  summary.changed_top_fields.push_back(top_field_number);

  ObjectMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.result_json = transform_summary_to_json(summary);
  mutation.changed_top_fields = std::move(summary.changed_top_fields);
  return mutation;
}

std::string vec3_json(const Vec3& value) {
  std::ostringstream out;
  out << "{"
      << "\"x\":" << value.x << ","
      << "\"y\":" << value.y << ","
      << "\"z\":" << value.z
      << "}";
  return out.str();
}

std::string transform_json(const Transform& transform) {
  std::ostringstream out;
  out << "{"
      << "\"position\":" << vec3_json(transform.position) << ","
      << "\"rotation\":" << vec3_json(transform.rotation) << ","
      << "\"scale\":" << vec3_json(transform.scale)
      << "}";
  return out.str();
}

std::string changed_top_fields_json(const std::vector<uint32_t>& fields) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) out << ",";
    out << fields[i];
  }
  out << "]";
  return out.str();
}

ObjectMutation make_object_mutation(
    const GilFile& file,
    std::vector<uint8_t> next_payload,
    std::string result_json,
    std::vector<uint32_t> changed_top_fields) {
  ObjectMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.result_json = std::move(result_json);
  mutation.changed_top_fields = std::move(changed_top_fields);
  return mutation;
}

std::string create_scene_object_result_json(
    std::string_view kind,
    uint64_t object_id,
    uint64_t asset_id,
    const Transform& transform,
    const std::vector<uint32_t>& changed_top_fields) {
  std::ostringstream out;
  out << "{"
      << "\"kind\":" << json::quote(std::string(kind)) << ","
      << "\"objectId\":" << object_id << ","
      << "\"assetId\":" << asset_id << ","
      << "\"transform\":" << transform_json(transform) << ","
      << "\"changedTopFields\":" << changed_top_fields_json(changed_top_fields)
      << "}";
  return out.str();
}

std::string create_prefab_result_json(
    uint64_t prefab_id,
    uint64_t asset_id,
    const Transform& transform,
    const std::vector<uint32_t>& changed_top_fields) {
  std::ostringstream out;
  out << "{"
      << "\"kind\":\"prefab\","
      << "\"prefabId\":" << prefab_id << ","
      << "\"assetId\":" << asset_id << ","
      << "\"transform\":" << transform_json(transform) << ","
      << "\"changedTopFields\":" << changed_top_fields_json(changed_top_fields)
      << "}";
  return out.str();
}

std::string create_scene_prefab_instance_result_json(
    uint64_t object_id,
    uint64_t prefab_id,
    uint64_t asset_id,
    const Transform& transform,
    const std::vector<uint32_t>& changed_top_fields) {
  std::ostringstream out;
  out << "{"
      << "\"kind\":\"scenePrefabInstance\","
      << "\"objectId\":" << object_id << ","
      << "\"prefabId\":" << prefab_id << ","
      << "\"assetId\":" << asset_id << ","
      << "\"transform\":" << transform_json(transform) << ","
      << "\"changedTopFields\":" << changed_top_fields_json(changed_top_fields)
      << "}";
  return out.str();
}

}  // namespace

ObjectMutation create_scene_object(const GilFile& file, uint64_t asset_id, const CreateSceneObjectOptions& options) {
  const auto top5 = top_level_data(file, 5);
  if (!top5) throw std::runtime_error("top-level field 5 not found");

  const uint64_t object_id = options.object_id.value_or(allocate_object_id(payload(file)));
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

  auto result = create_scene_object_result_json("sceneObject", object_id, asset_id, options.transform, changed_top_fields);
  return make_object_mutation(file, std::move(next_payload), std::move(result), std::move(changed_top_fields));
}

ObjectMutation create_prefab(
    const GilFile& file,
    uint64_t asset_id,
    const CreatePrefabOptions& options,
    const GilFile* template_file) {
  const auto top4 = top_level_data(file, 4);
  if (!top4) throw std::runtime_error("top-level field 4 not found");

  const uint64_t prefab_id = options.prefab_id.value_or(allocate_object_id(payload(file)));
  auto template_entry = find_prefab_template_entry(template_file, asset_id);
  auto entry = template_entry ? std::move(*template_entry) : find_template_entry(*top4, {asset_id, 1000000});
  const std::array<uint32_t, 1> id_path{1};
  const std::array<uint32_t, 1> asset_path{2};
  entry = replace_nested_varint(entry, id_path, prefab_id);
  entry = replace_nested_varint(entry, asset_path, asset_id);
  entry = replace_entry_transform(entry, 7, options.transform);

  std::vector<uint32_t> changed_top_fields{4};
  auto next_payload = replace_top_level_field_data(payload(file), 4, append_repeated_entry(*top4, 1, std::move(entry)));
  next_payload = patch_top6_mapping(next_payload, 100, prefab_id, {6, 3}, changed_top_fields);

  auto result = create_prefab_result_json(prefab_id, asset_id, options.transform, changed_top_fields);
  return make_object_mutation(file, std::move(next_payload), std::move(result), std::move(changed_top_fields));
}

ObjectMutation create_scene_prefab_instance(
    const GilFile& file,
    uint64_t prefab_id,
    uint64_t asset_id,
    const CreateScenePrefabInstanceOptions& options,
    const GilFile* template_file) {
  const auto top5 = top_level_data(file, 5);
  if (!top5) throw std::runtime_error("top-level field 5 not found");

  const uint64_t object_id = options.object_id.value_or(allocate_object_id(payload(file)));
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

  auto result = create_scene_prefab_instance_result_json(
      object_id,
      prefab_id,
      asset_id,
      options.transform,
      changed_top_fields);
  return make_object_mutation(file, std::move(next_payload), std::move(result), std::move(changed_top_fields));
}

ObjectMutation set_scene_transform(const GilFile& file, uint64_t object_id, const Transform& transform) {
  return set_space_transform(file, 5, 6, "sceneTransform", object_id, transform);
}

ObjectMutation set_preview_transform(const GilFile& file, uint64_t object_id, const Transform& transform) {
  return set_space_transform(file, 8, 6, "previewTransform", object_id, transform);
}

std::string transform_summary_to_json(const TransformSummary& summary) {
  std::ostringstream out;
  out << "{"
      << "\"kind\":" << json::quote(summary.kind) << ","
      << "\"objectId\":" << summary.object_id << ","
      << "\"transform\":" << transform_json(summary.transform) << ","
      << "\"changedTopFields\":[";
  for (size_t i = 0; i < summary.changed_top_fields.size(); ++i) {
    if (i) out << ",";
    out << summary.changed_top_fields[i];
  }
  out << "]}";
  return out.str();
}

}  // namespace opengil
