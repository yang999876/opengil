#include "opengil/object_ops.hpp"

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

}  // namespace

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
