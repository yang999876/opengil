#include "opengil/decoration_ops.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include "opengil/semantic.hpp"

namespace opengil {
namespace {

template <size_t N>
std::optional<uint64_t> read_varint_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_varint_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

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

void update_reference_count_slot(std::vector<uint64_t>& packed) {
  const int64_t count = (static_cast<int64_t>(packed.size()) - 2) * 5;
  if (packed.size() < 2) packed.resize(2, 0);
  packed[1] = count < 0
      ? static_cast<uint64_t>(static_cast<uint32_t>(count))
      : static_cast<uint64_t>(count);
}

std::optional<std::vector<uint8_t>> read_bytes_at_path(
    std::span<const uint8_t> message,
    std::span<const uint32_t> path) {
  if (path.empty()) return std::nullopt;
  const auto fields = parse_owned_fields_or_throw(message, "decoration path message");
  for (const auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 2) continue;
      return field.data;
    }
    if (field.wire != 2) continue;
    if (auto nested = read_bytes_at_path(field.data, path.subspan(1))) return nested;
  }
  return std::nullopt;
}

bool set_len_data_at_path(
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
    auto child_fields = parse_owned_fields_or_throw(field.data, "decoration len path child");
    if (set_len_data_at_path(child_fields, path.subspan(1), data)) {
      field.data = rebuild_message(child_fields);
      return true;
    }
  }
  return false;
}

std::vector<uint8_t> set_packed_varint_list_at_path(
    std::span<const uint8_t> message,
    std::span<const uint32_t> path,
    const std::vector<uint64_t>& values) {
  auto fields = parse_owned_fields_or_throw(message, "decoration packed path message");
  if (!set_len_data_at_path(fields, path, encode_packed_varints(values))) {
    throw std::runtime_error("packed varint path not found");
  }
  return rebuild_message(fields);
}

std::vector<uint8_t> set_prefab_decoration_refs(
    std::span<const uint8_t> prefab_entry,
    const std::vector<uint64_t>& values) {
  auto fields = parse_owned_fields_or_throw(prefab_entry, "prefab decoration refs entry");
  const auto encoded = encode_packed_varints(values);

  for (auto& field : fields) {
    if (field.number != 6 || field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "prefab decoration refs block");
    for (auto& child : child_fields) {
      if (child.number != 50 || child.wire != 2) continue;
      child.data = encoded;
      field.data = rebuild_message(child_fields);
      return rebuild_message(fields);
    }
    child_fields.push_back(make_len_field(50, encoded));
    field.data = rebuild_message(child_fields);
    return rebuild_message(fields);
  }

  fields.push_back(make_len_field(6, rebuild_message({
      make_len_field(50, encoded),
  })));
  return rebuild_message(fields);
}

std::vector<uint8_t> encode_vec3_sparse(const Vec3& value) {
  std::vector<OwnedField> fields;
  if (value.x != 0.0) fields.push_back(make_fixed32_field(1, static_cast<float>(value.x)));
  if (value.y != 0.0) fields.push_back(make_fixed32_field(2, static_cast<float>(value.y)));
  if (value.z != 0.0) fields.push_back(make_fixed32_field(3, static_cast<float>(value.z)));
  return rebuild_message(fields);
}

std::vector<uint8_t> build_name_block(const std::string& name) {
  return rebuild_message({
      make_varint_field(1, 1),
      make_len_field(11, rebuild_message({
          make_len_field(1, std::vector<uint8_t>(name.begin(), name.end())),
      })),
  });
}

std::vector<uint8_t> build_owner_block(uint64_t owner_id) {
  return rebuild_message({
      make_varint_field(1, 40),
      make_len_field(50, rebuild_message({
          make_varint_field(502, owner_id),
      })),
  });
}

std::vector<uint8_t> build_transform_section(const Transform& transform) {
  return rebuild_message({
      make_len_field(1, encode_vec3_sparse(transform.position)),
      make_len_field(2, encode_vec3_sparse(transform.rotation)),
      make_len_field(3, encode_vec3_sparse(transform.scale)),
  });
}

std::vector<uint8_t> build_prefab_decoration_entry(
    uint64_t decoration_id,
    const DecorationSpec& spec,
    uint64_t owner_prefab_id) {
  return rebuild_message({
      make_varint_field(1, decoration_id),
      make_varint_field(2, spec.asset_id),
      make_varint_field(3, 1),
      make_len_field(4, build_name_block(spec.name)),
      make_len_field(4, build_owner_block(owner_prefab_id)),
      make_len_field(5, rebuild_message({
          make_varint_field(1, 1),
          make_len_field(11, build_transform_section(spec.transform)),
      })),
      make_len_field(5, rebuild_message({
          make_varint_field(1, 5),
          make_len_field(15, {}),
      })),
      make_len_field(5, rebuild_message({
          make_varint_field(1, 2),
          make_len_field(12, {}),
      })),
      make_len_field(11, {}),
  });
}

std::vector<uint8_t> build_scene_decoration_entry(
    uint64_t decoration_id,
    const DecorationSpec& spec,
    uint64_t owner_object_id,
    uint64_t prefab_decoration_id) {
  return rebuild_message({
      make_varint_field(1, decoration_id),
      make_varint_field(2, spec.asset_id),
      make_len_field(4, build_name_block(spec.name)),
      make_len_field(4, build_owner_block(owner_object_id)),
      make_len_field(5, rebuild_message({
          make_varint_field(1, 1),
          make_len_field(11, build_transform_section(spec.transform)),
      })),
      make_len_field(5, rebuild_message({
          make_varint_field(1, 5),
          make_len_field(15, {}),
      })),
      make_len_field(5, rebuild_message({
          make_varint_field(1, 2),
          make_len_field(12, {}),
      })),
      make_len_field(12, rebuild_message({
          make_varint_field(1, prefab_decoration_id),
      })),
  });
}

std::vector<uint8_t> find_repeated_entry(
    std::span<const uint8_t> message,
    uint32_t repeated_field,
    std::span<const uint32_t> id_path,
    uint64_t id) {
  for (const auto& field : len_fields(message, repeated_field)) {
    const auto entry = field_data(message, field);
    if (read_varint_at_path(entry, id_path) == id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  return {};
}

std::vector<std::vector<uint8_t>> find_scene_entries(std::span<const uint8_t> top8, uint64_t prefab_id) {
  std::vector<std::vector<uint8_t>> entries;
  const std::array<uint32_t, 2> prefab_ref_path{2, 1};
  for (const auto& field : len_fields(top8, 1)) {
    const auto entry = field_data(top8, field);
    if (read_varint_path(entry, prefab_ref_path) == prefab_id) {
      entries.emplace_back(entry.begin(), entry.end());
    }
  }
  return entries;
}

void merge_max_entry_id(
    std::optional<uint64_t>& max_id,
    std::span<const uint8_t> message,
    uint32_t repeated_field) {
  const std::array<uint32_t, 1> id_path{1};
  for (const auto& field : parse_owned_fields_or_throw(message, "decoration top-level id scan")) {
    if (field.number != repeated_field) continue;
    if (field.wire != 2) continue;
    const auto id = read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), id_path);
    if (!id) continue;
    max_id = max_id ? std::max(*max_id, *id) : *id;
  }
}

uint64_t next_decoration_entry_id(const GilFile& file) {
  std::optional<uint64_t> max_id;
  for (uint32_t top_field : {4u, 5u, 8u}) {
    if (const auto top = top_level_data(file, top_field)) merge_max_entry_id(max_id, *top, 1);
  }
  if (const auto top27 = top_level_data(file, 27)) {
    merge_max_entry_id(max_id, *top27, 1);
    merge_max_entry_id(max_id, *top27, 2);
  }
  return max_id.value_or(0) + 1;
}

std::vector<uint8_t> replace_repeated_entry(
    std::span<const uint8_t> message,
    uint32_t repeated_field,
    std::span<const uint32_t> id_path,
    uint64_t id,
    const std::vector<uint8_t>& next_data) {
  auto fields = parse_owned_fields_or_throw(message, "decoration repeated entry message");
  bool changed = false;
  for (auto& field : fields) {
    if (changed || field.number != repeated_field || field.wire != 2) continue;
    if (read_varint_at_path(std::span<const uint8_t>(field.data.data(), field.data.size()), id_path) != id) continue;
    field.data = next_data;
    changed = true;
  }
  if (!changed) throw std::runtime_error("repeated entry target not found");
  return rebuild_message(fields);
}

std::vector<uint8_t> replace_all_scene_entries(
    std::span<const uint8_t> top8,
    const std::map<uint64_t, std::vector<uint64_t>>& scene_decoration_ids_by_object) {
  auto fields = parse_owned_fields_or_throw(top8, "decoration top8");
  bool changed = false;
  const std::array<uint32_t, 1> object_id_path{1};
  const std::array<uint32_t, 2> packed_path{5, 50};
  for (auto& field : fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto object_id = read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), object_id_path);
    if (!object_id) continue;
    const auto it = scene_decoration_ids_by_object.find(*object_id);
    if (it == scene_decoration_ids_by_object.end()) continue;
    const auto current = read_bytes_at_path(field.data, std::span<const uint32_t>(packed_path.data(), packed_path.size()));
    if (!current) throw std::runtime_error("scene decoration reference list not found");
    auto packed = decode_packed_varints(*current);
    packed.insert(packed.end(), it->second.begin(), it->second.end());
    update_reference_count_slot(packed);
    field.data = set_packed_varint_list_at_path(field.data, std::span<const uint32_t>(packed_path.data(), packed_path.size()), packed);
    changed = true;
  }
  return changed ? rebuild_message(fields) : std::vector<uint8_t>(top8.begin(), top8.end());
}

std::vector<uint8_t> insert_decoration_entries(
    std::span<const uint8_t> top27,
    const std::vector<std::vector<uint8_t>>& new_field1_entries,
    const std::vector<std::vector<uint8_t>>& new_field2_entries) {
  auto fields = parse_owned_fields_or_throw(top27, "decoration top27");
  if (!new_field1_entries.empty()) {
    auto last = fields.end();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
      if (it->number == 1) last = it;
    }
    std::vector<OwnedField> inserts;
    for (const auto& entry : new_field1_entries) inserts.push_back(make_len_field(1, entry));
    if (last == fields.end()) fields.insert(fields.end(), inserts.begin(), inserts.end());
    else fields.insert(last + 1, inserts.begin(), inserts.end());
  }
  if (!new_field2_entries.empty()) {
    auto last = fields.end();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
      if (it->number == 2) last = it;
    }
    std::vector<OwnedField> inserts;
    for (const auto& entry : new_field2_entries) inserts.push_back(make_len_field(2, entry));
    if (last == fields.end()) fields.insert(fields.end(), inserts.begin(), inserts.end());
    else fields.insert(last + 1, inserts.begin(), inserts.end());
  }
  return rebuild_message(fields);
}

}  // namespace

DecorationMutation add_prefab_decorations(
    const GilFile& file,
    uint64_t prefab_id,
    const std::vector<DecorationSpec>& specs) {
  if (specs.empty()) throw std::runtime_error("at least one decoration spec is required");
  for (const auto& spec : specs) {
    if (spec.asset_id == 0) throw std::runtime_error("decoration asset id is required");
    if (spec.name.empty()) throw std::runtime_error("decoration name is required");
  }

  const auto top4 = top_level_data(file, 4);
  const auto top27 = top_level_data(file, 27);
  if (!top4 || !top27) throw std::runtime_error("required top-level fields 4 and 27 not found");

  const std::array<uint32_t, 1> prefab_id_path{1};
  auto prefab_entry = find_repeated_entry(*top4, 1, std::span<const uint32_t>(prefab_id_path.data(), prefab_id_path.size()), prefab_id);
  if (prefab_entry.empty()) throw std::runtime_error("prefab id not found in top-level field 4");

  std::vector<std::vector<uint8_t>> scene_entries;
  std::optional<std::vector<uint8_t>> top8_bytes;
  if (const auto top8 = top_level_data(file, 8)) {
    top8_bytes = std::vector<uint8_t>(top8->begin(), top8->end());
    scene_entries = find_scene_entries(*top8, prefab_id);
  }

  uint64_t next_top27_id = next_decoration_entry_id(file);
  std::vector<std::vector<uint8_t>> new_field1_entries;
  std::vector<std::vector<uint8_t>> new_field2_entries;
  std::map<uint64_t, std::vector<uint64_t>> scene_decoration_ids_by_object;

  DecorationSummary summary;
  summary.prefab_id = prefab_id;
  summary.scene_instance_count = scene_entries.size();

  const std::array<uint32_t, 1> object_id_path{1};
  for (const auto& spec : specs) {
    const uint64_t prefab_decoration_id = next_top27_id++;
    summary.prefab_decoration_ids.push_back(prefab_decoration_id);
    new_field1_entries.push_back(build_prefab_decoration_entry(prefab_decoration_id, spec, prefab_id));

    for (const auto& scene_entry : scene_entries) {
      const auto object_id = read_varint_path(std::span<const uint8_t>(scene_entry.data(), scene_entry.size()), object_id_path);
      if (!object_id) continue;
      const uint64_t scene_decoration_id = next_top27_id++;
      summary.scene_decoration_ids.push_back(scene_decoration_id);
      scene_decoration_ids_by_object[*object_id].push_back(scene_decoration_id);
      new_field2_entries.push_back(build_scene_decoration_entry(
          scene_decoration_id,
          spec,
          *object_id,
          prefab_decoration_id));
    }
  }

  const std::array<uint32_t, 2> prefab_packed_path{6, 50};
  const auto prefab_packed_bytes = read_bytes_at_path(prefab_entry, std::span<const uint32_t>(prefab_packed_path.data(), prefab_packed_path.size()));
  auto prefab_packed = prefab_packed_bytes ? decode_packed_varints(*prefab_packed_bytes) : std::vector<uint64_t>{0, 0};
  prefab_packed.insert(prefab_packed.end(), summary.prefab_decoration_ids.begin(), summary.prefab_decoration_ids.end());
  update_reference_count_slot(prefab_packed);
  prefab_entry = set_prefab_decoration_refs(prefab_entry, prefab_packed);

  std::vector<uint8_t> next_payload(payload(file).begin(), payload(file).end());
  const auto next_top4 = replace_repeated_entry(
      *top4,
      1,
      std::span<const uint32_t>(prefab_id_path.data(), prefab_id_path.size()),
      prefab_id,
      prefab_entry);
  next_payload = replace_top_level_field_data(next_payload, 4, next_top4);
  summary.changed_top_fields.push_back(4);

  if (top8_bytes && !scene_decoration_ids_by_object.empty()) {
    const auto next_top8 = replace_all_scene_entries(*top8_bytes, scene_decoration_ids_by_object);
    next_payload = replace_top_level_field_data(next_payload, 8, next_top8);
    summary.changed_top_fields.push_back(8);
  }

  const auto next_top27 = insert_decoration_entries(*top27, new_field1_entries, new_field2_entries);
  next_payload = replace_top_level_field_data(next_payload, 27, next_top27);
  summary.changed_top_fields.push_back(27);

  DecorationMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

}  // namespace opengil
