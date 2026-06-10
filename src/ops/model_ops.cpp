#include "opengil/model_ops.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>

#include "opengil/json.hpp"
#include "opengil/semantic.hpp"

namespace opengil {
namespace {

template <size_t N>
std::optional<uint64_t> read_varint_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_varint_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

bool is_empty_model_marker(std::span<const uint8_t> group) {
  const std::array<uint32_t, 1> tag_path{1};
  const auto tag = read_varint_path(group, tag_path);
  if (tag != EMPTY_MODEL_TAG_ID) return false;
  const auto fields = parse_owned_fields_or_throw(group, "empty model marker group");
  return std::any_of(fields.begin(), fields.end(), [](const OwnedField& field) {
    return field.number == EMPTY_MODEL_PAYLOAD_FIELD && field.wire == 2;
  });
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

void replace_or_append_varint(std::vector<OwnedField>& fields, uint32_t number, uint64_t value) {
  for (auto& field : fields) {
    if (field.number == number && field.wire == 0) {
      field.varint = value;
      return;
    }
  }
  fields.push_back(make_varint_field(number, value));
}

OwnedField build_empty_model_marker_group() {
  std::vector<OwnedField> fields;
  fields.push_back(make_varint_field(1, EMPTY_MODEL_TAG_ID));
  fields.push_back(make_len_field(EMPTY_MODEL_PAYLOAD_FIELD, {}));
  return make_len_field(5, rebuild_message(fields));
}

void patch_empty_model_marker(std::vector<OwnedField>& entry_fields, uint64_t model_asset_id) {
  std::vector<OwnedField> next;
  next.reserve(entry_fields.size() + 1);
  bool had_marker = false;

  for (const auto& field : entry_fields) {
    if (field.number == 5 && field.wire == 2 && is_empty_model_marker(field.data)) {
      had_marker = true;
      if (model_asset_id != EMPTY_MODEL_ASSET_ID) continue;
    }
    next.push_back(field);
  }

  if (model_asset_id == EMPTY_MODEL_ASSET_ID && !had_marker) {
    auto marker = build_empty_model_marker_group();
    auto insert_at = std::find_if(next.begin(), next.end(), [](const OwnedField& field) {
      if (field.number != 5 || field.wire != 2) return false;
      const std::array<uint32_t, 1> tag_path{1};
      return read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), tag_path) == 52;
    });
    next.insert(insert_at, std::move(marker));
  }

  entry_fields = std::move(next);
}

std::vector<uint8_t> patch_prefab_top4(
    std::span<const uint8_t> top4,
    uint64_t prefab_id,
    uint64_t model_asset_id,
    SetModelSummary& summary) {
  auto fields = parse_owned_fields_or_throw(top4, "model top4");
  bool found = false;

  for (auto& field : fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto entry_span = std::span<const uint8_t>(field.data.data(), field.data.size());
    const std::array<uint32_t, 1> id_path{1};
    const auto id = read_varint_path(entry_span, id_path);
    if (id != prefab_id) continue;

    auto entry_fields = parse_owned_fields_or_throw(entry_span, "model prefab entry");
    replace_or_append_varint(entry_fields, 2, model_asset_id);
    field.data = rebuild_message(entry_fields);
    found = true;
    summary.prefab_updated = true;
    break;
  }

  if (!found) throw std::runtime_error("prefab id not found");
  return rebuild_message(fields);
}

std::vector<uint8_t> patch_scene_like_top(
    std::span<const uint8_t> top_data,
    uint64_t prefab_id,
    uint64_t model_asset_id,
    size_t& updated_count) {
  auto fields = parse_owned_fields_or_throw(top_data, "model scene-like top");
  updated_count = 0;

  for (auto& field : fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto entry_span = std::span<const uint8_t>(field.data.data(), field.data.size());
    const std::array<uint32_t, 2> prefab_ref_path{2, 1};
    const auto ref = read_varint_path(entry_span, prefab_ref_path);
    if (ref != prefab_id) continue;

    auto entry_fields = parse_owned_fields_or_throw(entry_span, "model scene-like entry");
    replace_or_append_varint(entry_fields, 8, model_asset_id);
    patch_empty_model_marker(entry_fields, model_asset_id);
    field.data = rebuild_message(entry_fields);
    updated_count++;
  }

  return rebuild_message(fields);
}

}  // namespace

GilMutation set_prefab_model_asset_id(const GilFile& file, uint64_t prefab_id, uint64_t model_asset_id) {
  const auto model_before = get_model_info(file, prefab_id);
  if (!model_before) throw std::runtime_error("prefab id not found");

  SetModelSummary summary;
  summary.prefab_id = prefab_id;
  summary.prefab_name = model_before->name;
  summary.model_asset_id = model_asset_id;

  const auto top4 = top_level_data(file, 4);
  if (!top4) throw std::runtime_error("top-level field 4 not found");

  auto next_top4 = patch_prefab_top4(*top4, prefab_id, model_asset_id, summary);
  std::vector<uint8_t> next_payload = replace_top_level_field_data(payload(file), 4, next_top4);
  summary.changed_top_fields.push_back(4);

  if (const auto top5 = top_level_data(file, 5)) {
    size_t updated = 0;
    auto next_top5 = patch_scene_like_top(*top5, prefab_id, model_asset_id, updated);
    if (updated > 0) {
      next_payload = replace_top_level_field_data(next_payload, 5, next_top5);
      summary.scene_updated = updated;
      summary.changed_top_fields.push_back(5);
    }
  }

  if (const auto top8 = top_level_data(file, 8)) {
    size_t updated = 0;
    auto next_top8 = patch_scene_like_top(*top8, prefab_id, model_asset_id, updated);
    if (updated > 0) {
      next_payload = replace_top_level_field_data(next_payload, 8, next_top8);
      summary.preview_updated = updated;
      summary.changed_top_fields.push_back(8);
    }
  }

  GilMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.model_summary = std::move(summary);
  return mutation;
}

GilMutation set_prefab_to_empty_model(const GilFile& file, uint64_t prefab_id) {
  return set_prefab_model_asset_id(file, prefab_id, EMPTY_MODEL_ASSET_ID);
}

std::string set_model_summary_to_json(const SetModelSummary& summary) {
  std::ostringstream out;
  out << "{"
      << "\"prefabId\":" << summary.prefab_id << ","
      << "\"prefabName\":" << json::quote(summary.prefab_name) << ","
      << "\"modelAssetId\":" << summary.model_asset_id << ","
      << "\"prefabUpdated\":" << json::bool_value(summary.prefab_updated) << ","
      << "\"sceneUpdated\":" << summary.scene_updated << ","
      << "\"previewUpdated\":" << summary.preview_updated << ","
      << "\"changedTopFields\":[";
  for (size_t i = 0; i < summary.changed_top_fields.size(); ++i) {
    if (i) out << ",";
    out << summary.changed_top_fields[i];
  }
  out << "]}";
  return out.str();
}

}  // namespace opengil
