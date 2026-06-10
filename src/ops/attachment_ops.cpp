#include "opengil/attachment_ops.hpp"

#include <array>
#include <cstring>
#include <functional>
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

template <size_t N>
std::optional<std::string> read_string_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_string_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
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

std::vector<uint8_t> string_bytes(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> build_vector2(double x, double y) {
  std::vector<OwnedField> fields;
  if (x != 0.0) fields.push_back(make_fixed32_field(1, static_cast<float>(x)));
  if (y != 0.0) fields.push_back(make_fixed32_field(2, static_cast<float>(y)));
  return rebuild_message(fields);
}

std::vector<uint8_t> build_attachment21_entry(const AttachmentPointSpec& spec) {
  return rebuild_message({
      make_len_field(1, string_bytes(spec.name)),
      make_len_field(2, build_vector2(spec.x, spec.y)),
      make_len_field(3, build_vector2(spec.rot_x, spec.rot_y)),
      make_len_field(502, string_bytes(spec.name)),
      make_varint_field(504, 1),
      make_len_field(505, string_bytes(spec.display_name)),
  });
}

std::vector<uint8_t> build_attachment32_entry(const AttachmentPointSpec& spec) {
  return rebuild_message({
      make_len_field(1, string_bytes(spec.name)),
      make_len_field(2, build_vector2(spec.x, spec.y)),
      make_len_field(3, build_vector2(spec.rot_x, spec.rot_y)),
      make_varint_field(501, 1001),
      make_len_field(502, string_bytes(spec.name)),
      make_varint_field(504, 1),
      make_len_field(505, string_bytes(spec.display_name)),
  });
}

std::vector<uint8_t> upsert_repeated_submessage_in_wrapper(
    std::span<const uint8_t> message,
    uint32_t wrapper_field_no,
    const std::function<bool(std::span<const uint8_t>)>& wrapper_predicate,
    uint32_t container_field_no,
    uint32_t repeated_field_no,
    const std::string& name,
    const std::vector<uint8_t>& new_item_data) {
  auto fields = parse_owned_fields_or_throw(message, "attachment wrapper host message");
  bool changed = false;
  const std::array<uint32_t, 1> name_path{1};

  for (auto& field : fields) {
    if (changed || field.number != wrapper_field_no || field.wire != 2) continue;
    if (!wrapper_predicate(std::span<const uint8_t>(field.data.data(), field.data.size()))) continue;

    auto wrapper_fields = parse_owned_fields_or_throw(field.data, "attachment wrapper");
    bool wrapper_changed = false;
    for (auto& container_field : wrapper_fields) {
      if (wrapper_changed || container_field.number != container_field_no || container_field.wire != 2) continue;

      auto container_fields = parse_owned_fields_or_throw(container_field.data, "attachment container");
      bool upserted = false;
      for (auto& item_field : container_fields) {
        if (item_field.number != repeated_field_no || item_field.wire != 2) continue;
        if (read_string_path(std::span<const uint8_t>(item_field.data.data(), item_field.data.size()), name_path) != name) continue;
        item_field.data = new_item_data;
        upserted = true;
        break;
      }
      if (!upserted) container_fields.push_back(make_len_field(repeated_field_no, new_item_data));
      container_field.data = rebuild_message(container_fields);
      wrapper_changed = true;
    }
    if (!wrapper_changed) throw std::runtime_error("attachment container field not found");
    field.data = rebuild_message(wrapper_fields);
    changed = true;
  }
  if (!changed) throw std::runtime_error("attachment wrapper field not found");
  return rebuild_message(fields);
}

std::vector<uint8_t> find_prefab_entry(std::span<const uint8_t> top4, uint64_t prefab_id) {
  const std::array<uint32_t, 1> id_path{1};
  for (const auto& field : len_fields(top4, 1)) {
    const auto entry = field_data(top4, field);
    if (read_varint_path(entry, id_path) == prefab_id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  return {};
}

std::vector<uint8_t> replace_prefab_entry(std::span<const uint8_t> top4, uint64_t prefab_id, const std::vector<uint8_t>& entry) {
  auto fields = parse_owned_fields_or_throw(top4, "attachment top4");
  const std::array<uint32_t, 1> id_path{1};
  bool changed = false;
  for (auto& field : fields) {
    if (changed || field.number != 1 || field.wire != 2) continue;
    if (read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), id_path) != prefab_id) continue;
    field.data = entry;
    changed = true;
  }
  if (!changed) throw std::runtime_error("prefab entry not found");
  return rebuild_message(fields);
}

std::vector<std::vector<uint8_t>> find_scene_entries(
    std::span<const uint8_t> top8,
    uint64_t prefab_id,
    std::optional<uint64_t> object_id) {
  std::vector<std::vector<uint8_t>> entries;
  const std::array<uint32_t, 1> id_path{1};
  const std::array<uint32_t, 2> prefab_ref_path{2, 1};
  for (const auto& field : len_fields(top8, 1)) {
    const auto entry = field_data(top8, field);
    const bool match = object_id
        ? read_varint_path(entry, id_path) == *object_id
        : read_varint_path(entry, prefab_ref_path) == prefab_id;
    if (match) entries.emplace_back(entry.begin(), entry.end());
  }
  return entries;
}

std::vector<uint8_t> replace_scene_entries(
    std::span<const uint8_t> top8,
    const std::vector<std::vector<uint8_t>>& scene_entries,
    const std::vector<AttachmentPointSpec>& specs) {
  auto fields = parse_owned_fields_or_throw(top8, "attachment top8");
  bool changed = false;
  const std::array<uint32_t, 1> id_path{1};

  for (auto& field : fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto object_id = read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), id_path);
    if (!object_id) continue;

    bool target = false;
    for (const auto& entry : scene_entries) {
      if (read_varint_path(std::span<const uint8_t>(entry.data(), entry.size()), id_path) == *object_id) {
        target = true;
        break;
      }
    }
    if (!target) continue;

    auto next_entry = field.data;
    for (const auto& spec : specs) {
      next_entry = upsert_repeated_submessage_in_wrapper(
          next_entry,
          6,
          [](std::span<const uint8_t> wrapper) {
            const std::array<uint32_t, 1> tag_path{1};
            return read_varint_at_path(wrapper, std::span<const uint32_t>(tag_path.data(), tag_path.size())) == 11;
          },
          21,
          1,
          spec.name,
          build_attachment21_entry(spec));
      next_entry = upsert_repeated_submessage_in_wrapper(
          next_entry,
          7,
          [](std::span<const uint8_t> wrapper) {
            const std::array<uint32_t, 1> tag_path{1};
            const std::array<uint32_t, 1> enabled_path{2};
            return read_varint_at_path(wrapper, std::span<const uint32_t>(tag_path.data(), tag_path.size())) == 21 &&
                   read_varint_at_path(wrapper, std::span<const uint32_t>(enabled_path.data(), enabled_path.size())) == 1;
          },
          32,
          501,
          spec.name,
          build_attachment32_entry(spec));
    }
    field.data = std::move(next_entry);
    changed = true;
  }

  return changed ? rebuild_message(fields) : std::vector<uint8_t>(top8.begin(), top8.end());
}

}  // namespace

AttachmentMutation add_attachment_points(
    const GilFile& file,
    uint64_t prefab_id,
    std::optional<uint64_t> object_id,
    const std::vector<AttachmentPointSpec>& specs) {
  if (specs.empty()) throw std::runtime_error("at least one attachment point spec is required");
  for (const auto& spec : specs) {
    if (spec.name.empty()) throw std::runtime_error("attachment name is required");
    if (spec.display_name.empty()) throw std::runtime_error("attachment display name is required");
  }

  const auto top4 = top_level_data(file, 4);
  const auto top8 = top_level_data(file, 8);
  if (!top4 || !top8) throw std::runtime_error("required top-level fields 4 and 8 not found");

  auto next_prefab_entry = find_prefab_entry(*top4, prefab_id);
  if (next_prefab_entry.empty()) throw std::runtime_error("prefab id not found in top-level field 4");

  for (const auto& spec : specs) {
    next_prefab_entry = upsert_repeated_submessage_in_wrapper(
        next_prefab_entry,
        7,
        [](std::span<const uint8_t> wrapper) {
          const std::array<uint32_t, 1> tag_path{1};
          return read_varint_at_path(wrapper, std::span<const uint32_t>(tag_path.data(), tag_path.size())) == 11;
        },
        21,
        1,
        spec.name,
        build_attachment21_entry(spec));
    next_prefab_entry = upsert_repeated_submessage_in_wrapper(
        next_prefab_entry,
        8,
        [](std::span<const uint8_t> wrapper) {
          const std::array<uint32_t, 1> tag_path{1};
          const std::array<uint32_t, 1> enabled_path{2};
          return read_varint_at_path(wrapper, std::span<const uint32_t>(tag_path.data(), tag_path.size())) == 21 &&
                 read_varint_at_path(wrapper, std::span<const uint32_t>(enabled_path.data(), enabled_path.size())) == 1;
        },
        32,
        501,
        spec.name,
        build_attachment32_entry(spec));
  }

  const auto scene_entries = find_scene_entries(*top8, prefab_id, object_id);
  if (scene_entries.empty()) throw std::runtime_error("no scene entries found");

  auto next_payload = std::vector<uint8_t>(payload(file).begin(), payload(file).end());
  const auto next_top4 = replace_prefab_entry(*top4, prefab_id, next_prefab_entry);
  next_payload = replace_top_level_field_data(next_payload, 4, next_top4);

  const auto next_top8 = replace_scene_entries(*top8, scene_entries, specs);
  next_payload = replace_top_level_field_data(next_payload, 8, next_top8);

  AttachmentSummary summary;
  summary.prefab_id = prefab_id;
  summary.object_id = object_id;
  summary.scene_instance_count = scene_entries.size();
  summary.changed_top_fields = {4, 8};
  for (const auto& spec : specs) summary.names.push_back(spec.name);

  AttachmentMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

}  // namespace opengil
