#include "opengil/prefab_ops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "opengil/json.hpp"
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

template <size_t N>
std::optional<float> read_fixed32_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_fixed32_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
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

bool replace_string_at_path(std::vector<OwnedField>& fields, std::span<const uint32_t> path, const std::string& text) {
  if (path.empty()) return false;
  for (auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 2) continue;
      field.data.assign(text.begin(), text.end());
      return true;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "prefab string path child");
    if (replace_string_at_path(child_fields, path.subspan(1), text)) {
      field.data = rebuild_message(child_fields);
      return true;
    }
  }
  return false;
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
    auto child_fields = parse_owned_fields_or_throw(field.data, "prefab varint path child");
    if (replace_varint_at_path(child_fields, path.subspan(1), value)) {
      field.data = rebuild_message(child_fields);
      return true;
    }
  }
  return false;
}

std::vector<uint8_t> fixed32_bytes(float value) {
  uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof(float));
  return {
      static_cast<uint8_t>(raw & 0xff),
      static_cast<uint8_t>((raw >> 8) & 0xff),
      static_cast<uint8_t>((raw >> 16) & 0xff),
      static_cast<uint8_t>((raw >> 24) & 0xff),
  };
}

bool replace_fixed32_at_path(std::vector<OwnedField>& fields, std::span<const uint32_t> path, float value) {
  if (path.empty()) return false;
  for (auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) {
      if (field.wire != 5) continue;
      field.data = fixed32_bytes(value);
      return true;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "prefab fixed32 path child");
    if (replace_fixed32_at_path(child_fields, path.subspan(1), value)) {
      field.data = rebuild_message(child_fields);
      return true;
    }
  }
  return false;
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
    auto child_fields = parse_owned_fields_or_throw(field.data, "prefab len path child");
    if (set_len_data_at_path(child_fields, path.subspan(1), data)) {
      field.data = rebuild_message(child_fields);
      return true;
    }
  }
  return false;
}

bool replace_prefab_name(std::vector<OwnedField>& entry_fields, const std::string& new_name) {
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

std::vector<uint8_t> replace_nested_varint(
    std::span<const uint8_t> message,
    std::span<const uint32_t> path,
    uint64_t value) {
  auto fields = parse_owned_fields_or_throw(message, "prefab nested varint message");
  if (!replace_varint_at_path(fields, path, value)) {
    throw std::runtime_error("nested varint path not found");
  }
  return rebuild_message(fields);
}

std::vector<uint8_t> replace_nested_fixed32(
    std::span<const uint8_t> message,
    std::span<const uint32_t> path,
    float value) {
  auto fields = parse_owned_fields_or_throw(message, "prefab nested fixed32 message");
  if (!replace_fixed32_at_path(fields, path, value)) {
    throw std::runtime_error("nested fixed32 path not found");
  }
  return rebuild_message(fields);
}

std::optional<std::vector<uint8_t>> read_bytes_at_path(
    std::span<const uint8_t> message,
    std::span<const uint32_t> path) {
  if (path.empty()) return std::nullopt;
  const auto fields = parse_owned_fields_or_throw(message, "prefab bytes path message");
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

std::vector<uint64_t> decode_packed_varints(std::span<const uint8_t> data) {
  std::vector<uint64_t> values;
  size_t offset = 0;
  while (offset < data.size()) {
    const auto value = read_varint(data, offset);
    if (!value) throw std::runtime_error("invalid packed varint list");
    if (value->next <= offset) throw std::runtime_error("packed varint parser made no progress");
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

std::vector<uint8_t> replace_reference_list_at_path(
    std::span<const uint8_t> message,
    std::span<const uint32_t> path,
    size_t old_reference_count,
    const std::vector<uint64_t>& new_reference_ids) {
  const auto current = read_bytes_at_path(message, path);
  if (!current) throw std::runtime_error("reference list path not found");
  auto packed = decode_packed_varints(*current);
  if (packed.size() < old_reference_count) {
    throw std::runtime_error("reference list shorter than source decoration count");
  }
  std::vector<uint64_t> next(packed.begin(), packed.end() - static_cast<std::ptrdiff_t>(old_reference_count));
  if (next.size() >= 2) next[1] = static_cast<uint64_t>(new_reference_ids.size() * 5);
  next.insert(next.end(), new_reference_ids.begin(), new_reference_ids.end());

  auto fields = parse_owned_fields_or_throw(message, "prefab reference list message");
  if (!set_len_data_at_path(fields, path, encode_packed_varints(next))) {
    throw std::runtime_error("reference list path not found");
  }
  return rebuild_message(fields);
}

std::vector<uint8_t> replace_reference_list_in_joint_wrapper(
    std::span<const uint8_t> message,
    size_t old_reference_count,
    const std::vector<uint64_t>& new_reference_ids) {
  auto fields = parse_owned_fields_or_throw(message, "prefab joint reference wrapper host");
  bool changed = false;
  for (auto& field : fields) {
    if (changed || field.number != 6 || field.wire != 2) continue;
    const std::array<uint32_t, 1> wrapper_tag_path{1};
    if (read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), wrapper_tag_path) != 40) {
      continue;
    }

    auto wrapper_fields = parse_owned_fields_or_throw(field.data, "prefab joint reference wrapper");
    for (auto& wrapper_field : wrapper_fields) {
      if (wrapper_field.number != 50 || wrapper_field.wire != 2) continue;
      auto packed = decode_packed_varints(wrapper_field.data);
      if (packed.size() < old_reference_count) {
        throw std::runtime_error("joint reference list shorter than source decoration count");
      }
      std::vector<uint64_t> next(packed.begin(), packed.end() - static_cast<std::ptrdiff_t>(old_reference_count));
      if (next.size() >= 2) next[1] = static_cast<uint64_t>(new_reference_ids.size() * 5);
      next.insert(next.end(), new_reference_ids.begin(), new_reference_ids.end());
      wrapper_field.data = encode_packed_varints(next);
      field.data = rebuild_message(wrapper_fields);
      changed = true;
      break;
    }
  }
  if (!changed) throw std::runtime_error("joint reference wrapper not found");
  return rebuild_message(fields);
}

std::vector<uint8_t> replace_any_reference_list(
    std::span<const uint8_t> message,
    size_t old_reference_count,
    const std::vector<uint64_t>& new_reference_ids) {
  if (old_reference_count == 0) return std::vector<uint8_t>(message.begin(), message.end());

  const auto fields = parse_owned_fields_or_throw(message, "prefab reference list probe");
  const std::array<uint32_t, 1> wrapper_tag_path{1};
  const bool has_joint_wrapper = std::any_of(fields.begin(), fields.end(), [&](const OwnedField& field) {
    return field.number == 6 &&
           field.wire == 2 &&
           read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), wrapper_tag_path) == 40;
  });
  if (has_joint_wrapper) return replace_reference_list_in_joint_wrapper(message, old_reference_count, new_reference_ids);

  const std::array<uint32_t, 2> plain_reference_path{6, 50};
  if (read_bytes_at_path(message, std::span<const uint32_t>(plain_reference_path.data(), plain_reference_path.size()))) {
    return replace_reference_list_at_path(
        message,
        std::span<const uint32_t>(plain_reference_path.data(), plain_reference_path.size()),
        old_reference_count,
        new_reference_ids);
  }
  throw std::runtime_error("reference list not found in prefab entry");
}

std::string read_prefab_name_from_entry(std::span<const uint8_t> entry) {
  const std::array<uint32_t, 3> preferred{6, 11, 1};
  if (auto text = read_string_path(entry, preferred)) return normalize_visible_text(*text);
  const std::array<uint32_t, 2> fallback1{6, 11};
  if (auto text = read_string_path(entry, fallback1)) return normalize_visible_text(*text);
  const std::array<uint32_t, 1> fallback2{3};
  if (auto text = read_string_path(entry, fallback2)) return normalize_visible_text(*text);
  return "";
}

std::string find_prefab_name(const GilFile& file, uint64_t prefab_id) {
  for (const auto& prefab : list_prefabs(file)) {
    if (prefab.prefab_id == prefab_id) return prefab.name;
  }
  throw std::runtime_error("prefab id not found");
}

std::vector<uint8_t> patch_top4_name(std::span<const uint8_t> top4, uint64_t prefab_id, const std::string& new_name) {
  auto fields = parse_owned_fields_or_throw(top4, "prefab top4 rename");
  bool found = false;

  for (auto& field : fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto entry_span = std::span<const uint8_t>(field.data.data(), field.data.size());
    const std::array<uint32_t, 1> id_path{1};
    const auto id = read_varint_path(entry_span, id_path);
    if (id != prefab_id) continue;

    auto entry_fields = parse_owned_fields_or_throw(entry_span, "prefab rename entry");
    if (!replace_prefab_name(entry_fields, new_name)) {
      throw std::runtime_error("prefab name path not found");
    }
    field.data = rebuild_message(entry_fields);
    found = true;
    break;
  }

  if (!found) throw std::runtime_error("prefab id not found");
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

std::vector<uint8_t> find_source4_entry(std::span<const uint8_t> top4, uint64_t prefab_id) {
  for (const auto& field : len_fields(top4, 1)) {
    const auto entry = field_data(top4, field);
    const std::array<uint32_t, 1> id_path{1};
    if (read_varint_path(entry, id_path) == prefab_id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  throw std::runtime_error("source prefab id not found in top-level field 4");
}

void ensure_prefab_name_does_not_exist(std::span<const uint8_t> top4, const std::string& name) {
  for (const auto& field : len_fields(top4, 1)) {
    const auto entry = field_data(top4, field);
    if (read_prefab_name_from_entry(entry) == name) {
      throw std::runtime_error("prefab name already exists: " + name);
    }
  }
}

std::vector<std::vector<uint8_t>> find_source27_items(std::span<const uint8_t> top27, uint64_t prefab_id) {
  std::vector<std::vector<uint8_t>> items;
  for (const auto& field : len_fields(top27, 1)) {
    const auto item = field_data(top27, field);
    const std::array<uint32_t, 3> owner_path{4, 50, 502};
    if (read_varint_path(item, owner_path) == prefab_id) {
      items.emplace_back(item.begin(), item.end());
    }
  }
  return items;
}

std::vector<uint64_t> collect_top27_ids(std::span<const uint8_t> top27) {
  std::vector<uint64_t> ids;
  for (uint32_t field_number : {1u, 2u}) {
    for (const auto& field : len_fields(top27, field_number)) {
      const auto item = field_data(top27, field);
      const std::array<uint32_t, 1> id_path{1};
      if (auto id = read_varint_path(item, id_path)) ids.push_back(*id);
    }
  }
  return ids;
}

std::vector<uint64_t> collect_top_entry_ids(std::span<const uint8_t> payload_bytes, uint32_t top_field_number) {
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

std::vector<uint64_t> collect_user_prefab_ids(std::span<const uint8_t> payload_bytes) {
  std::vector<uint64_t> ids;
  const auto top4 = top_level_data_from_payload(payload_bytes, 4);
  if (!top4) return ids;
  for (const auto& field : len_fields(*top4, 1)) {
    const auto entry = field_data(*top4, field);
    const std::array<uint32_t, 1> id_path{1};
    const std::array<uint32_t, 1> model_path{2};
    const auto id = read_varint_path(entry, id_path);
    const auto model = read_varint_path(entry, model_path);
    if (id && model && *model != 1000000 && *model != 1000001) ids.push_back(*id);
  }
  return ids;
}

std::set<uint64_t> collect_occupied_object_ids(
    std::span<const uint8_t> payload_bytes,
    const std::optional<std::vector<uint8_t>>& top27) {
  std::set<uint64_t> ids;
  for (uint32_t top_field : {4u, 5u, 8u}) {
    for (uint64_t id : collect_top_entry_ids(payload_bytes, top_field)) ids.insert(id);
  }
  if (top27) {
    for (uint64_t id : collect_top27_ids(*top27)) ids.insert(id);
  }
  return ids;
}

uint64_t allocate_prefab_id(
    std::span<const uint8_t> payload_bytes,
    const std::optional<std::vector<uint8_t>>& top27,
    std::optional<uint64_t> start_after_id) {
  const auto occupied = collect_occupied_object_ids(payload_bytes, top27);
  const auto user_prefab_ids = collect_user_prefab_ids(payload_bytes);
  uint64_t anchor = start_after_id.value_or(1077936128);
  if (!start_after_id && !user_prefab_ids.empty()) {
    anchor = std::max<uint64_t>(anchor, *std::max_element(user_prefab_ids.begin(), user_prefab_ids.end()));
  }
  uint64_t next = anchor + 1;
  while (occupied.contains(next)) ++next;
  return next;
}

double round6(double value) {
  return std::round(value * 1000000.0) / 1000000.0;
}

std::pair<double, double> compute_next_preview_position(
    std::span<const uint8_t> payload_bytes,
    std::span<const uint8_t> source_entry,
    double preview_x_step,
    double preview_z_step) {
  const std::array<uint32_t, 4> source_x_path{7, 11, 1, 1};
  const std::array<uint32_t, 4> source_z_path{7, 11, 1, 3};
  const double source_x = read_fixed32_path(source_entry, source_x_path).value_or(0.0f);
  const double source_z = read_fixed32_path(source_entry, source_z_path).value_or(0.0f);
  double max_x = source_x;
  double max_z = source_z;
  bool saw_anchor = false;

  const auto top4 = top_level_data_from_payload(payload_bytes, 4);
  if (top4) {
    for (const auto& field : len_fields(*top4, 1)) {
      const auto entry = field_data(*top4, field);
      const auto x = read_fixed32_path(entry, source_x_path);
      const auto z = read_fixed32_path(entry, source_z_path);
      if (!x || !z) continue;
      max_x = saw_anchor ? std::max(max_x, static_cast<double>(*x)) : static_cast<double>(*x);
      max_z = saw_anchor ? std::max(max_z, static_cast<double>(*z)) : static_cast<double>(*z);
      saw_anchor = true;
    }
  }

  return {round6(max_x + preview_x_step), round6(max_z + preview_z_step)};
}

std::vector<uint8_t> clone4_entry(
    std::span<const uint8_t> source_entry,
    uint64_t new_prefab_id,
    const std::string& new_name,
    const std::vector<uint64_t>& source_reference_ids,
    const std::vector<uint64_t>& new_reference_ids,
    double preview_x,
    double preview_z) {
  auto fields = parse_owned_fields_or_throw(source_entry, "prefab clone source entry");
  const std::array<uint32_t, 1> id_path{1};
  if (!replace_varint_at_path(fields, std::span<const uint32_t>(id_path.data(), id_path.size()), new_prefab_id)) {
    throw std::runtime_error("prefab id path not found");
  }
  if (!replace_prefab_name(fields, new_name)) {
    throw std::runtime_error("prefab name path not found");
  }
  auto next = rebuild_message(fields);

  if (!source_reference_ids.empty()) {
    next = replace_any_reference_list(next, source_reference_ids.size(), new_reference_ids);
  }

  const std::array<uint32_t, 4> preview_x_path{7, 11, 1, 1};
  const std::array<uint32_t, 4> preview_z_path{7, 11, 1, 3};
  next = replace_nested_fixed32(next, std::span<const uint32_t>(preview_x_path.data(), preview_x_path.size()), static_cast<float>(preview_x));
  next = replace_nested_fixed32(next, std::span<const uint32_t>(preview_z_path.data(), preview_z_path.size()), static_cast<float>(preview_z));
  return next;
}

std::vector<uint8_t> clone27_item(std::span<const uint8_t> source_item, uint64_t new_decoration_id, uint64_t new_prefab_id) {
  const std::array<uint32_t, 1> id_path{1};
  auto next = replace_nested_varint(source_item, std::span<const uint32_t>(id_path.data(), id_path.size()), new_decoration_id);
  const std::array<uint32_t, 3> owner_path{4, 50, 502};
  next = replace_nested_varint(next, std::span<const uint32_t>(owner_path.data(), owner_path.size()), new_prefab_id);
  return next;
}

std::vector<uint8_t> insert_prefab_entry_before_source(
    std::span<const uint8_t> top4,
    uint64_t source_prefab_id,
    std::vector<uint8_t> new_entry) {
  const auto fields = parse_owned_fields_or_throw(top4, "prefab top4 insertion");
  std::vector<OwnedField> next;
  next.reserve(fields.size() + 1);
  bool inserted = false;
  for (const auto& field : fields) {
    if (!inserted && field.number == 1 && field.wire == 2) {
      const std::array<uint32_t, 1> id_path{1};
      if (read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), id_path) == source_prefab_id) {
        next.push_back(make_len_field(1, new_entry));
        inserted = true;
      }
    }
    next.push_back(field);
  }
  if (!inserted) throw std::runtime_error("source prefab entry not found for insertion");
  return rebuild_message(next);
}

std::vector<uint8_t> build_prefab_mapping(uint64_t prefab_id) {
  return rebuild_message({
      make_varint_field(1, 100),
      make_varint_field(2, prefab_id),
  });
}

std::vector<uint8_t> append_mapping_to_category(
    std::span<const uint8_t> top6,
    const std::optional<uint64_t>& target_tab_id,
    const std::string& target_tab_name,
    uint64_t new_prefab_id) {
  auto fields = parse_owned_fields_or_throw(top6, "prefab top6 category mappings");
  bool changed = false;
  for (auto& field : fields) {
    if (changed || field.number != 1 || field.wire != 2) continue;
    const std::array<uint32_t, 1> category_path{1};
    if (read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), category_path) != 6) continue;

    auto entry_fields = parse_owned_fields_or_throw(field.data, "prefab top6 category entry");
    for (auto& entry_field : entry_fields) {
      if (changed || entry_field.number != 2 || entry_field.wire != 2) continue;
      auto root_fields = parse_owned_fields_or_throw(entry_field.data, "prefab top6 category root");
      for (auto& root_field : root_fields) {
        if (changed || root_field.number != 4 || root_field.wire != 2) continue;
        const std::span<const uint8_t> child(root_field.data.data(), root_field.data.size());
        const std::array<uint32_t, 1> tab_id_path{3};
        const std::array<uint32_t, 1> tab_name_path{1};
        const auto id = read_varint_path(child, tab_id_path);
        const auto name = read_string_path(child, tab_name_path);
        const bool hit_by_id = target_tab_id && id == *target_tab_id;
        const bool hit_by_name = !target_tab_id && name && normalize_visible_text(*name) == target_tab_name;
        if (!hit_by_id && !hit_by_name) continue;

        auto child_fields = parse_owned_fields_or_throw(root_field.data, "prefab top6 category child");
        child_fields.push_back(make_len_field(5, build_prefab_mapping(new_prefab_id)));
        root_field.data = rebuild_message(child_fields);
        entry_field.data = rebuild_message(root_fields);
        field.data = rebuild_message(entry_fields);
        changed = true;
      }
    }
  }
  if (!changed) {
    throw std::runtime_error(target_tab_id ? "target tab id not found" : "target tab name not found");
  }
  return rebuild_message(fields);
}

std::vector<uint8_t> append_unclassified_prefab_mapping(std::span<const uint8_t> top6, uint64_t new_prefab_id) {
  auto fields = parse_owned_fields_or_throw(top6, "prefab top6 unclassified mappings");
  bool changed = false;
  for (auto& field : fields) {
    if (changed || field.number != 1 || field.wire != 2) continue;
    const std::array<uint32_t, 1> category_path{1};
    if (read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), category_path) != 3) continue;

    auto entry_fields = parse_owned_fields_or_throw(field.data, "prefab top6 unclassified entry");
    for (auto& entry_field : entry_fields) {
      if (changed || entry_field.number != 3 || entry_field.wire != 2) continue;
      auto child_fields = parse_owned_fields_or_throw(entry_field.data, "prefab top6 unclassified child");
      child_fields.push_back(make_len_field(5, build_prefab_mapping(new_prefab_id)));
      entry_field.data = rebuild_message(child_fields);
      field.data = rebuild_message(entry_fields);
      changed = true;
    }
  }
  if (!changed) throw std::runtime_error("unclassified prefab mapping target not found");
  return rebuild_message(fields);
}

std::vector<uint8_t> append_cloned_top27_items(
    std::span<const uint8_t> top27,
    const std::vector<std::vector<uint8_t>>& source_items,
    const std::vector<uint64_t>& new_decoration_ids,
    uint64_t new_prefab_id) {
  auto fields = parse_owned_fields_or_throw(top27, "prefab top27 clone append");
  for (size_t i = 0; i < source_items.size(); ++i) {
    fields.push_back(make_len_field(1, clone27_item(source_items[i], new_decoration_ids[i], new_prefab_id)));
  }
  return rebuild_message(fields);
}

std::vector<uint8_t> remove_prefab_from_top4(std::span<const uint8_t> top4, uint64_t prefab_id, bool& removed) {
  auto fields = parse_owned_fields_or_throw(top4, "prefab top4 removal");
  std::vector<OwnedField> next;
  const std::array<uint32_t, 1> id_path{1};
  for (auto& field : fields) {
    if (field.number == 1 &&
        field.wire == 2 &&
        read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), id_path) == prefab_id) {
      removed = true;
      continue;
    }
    next.push_back(std::move(field));
  }
  return rebuild_message(next);
}

std::vector<uint64_t> collect_top27_decoration_ids_for_prefab(std::span<const uint8_t> top27, uint64_t prefab_id) {
  std::vector<uint64_t> ids;
  const std::array<uint32_t, 1> id_path{1};
  const std::array<uint32_t, 3> owner_path{4, 50, 502};
  for (const auto& field : len_fields(top27, 1)) {
    const auto item = field_data(top27, field);
    if (read_varint_path(item, owner_path) != prefab_id) continue;
    if (auto id = read_varint_path(item, id_path)) ids.push_back(*id);
  }
  return ids;
}

std::vector<uint8_t> remove_top27_entries_for_prefab(std::span<const uint8_t> top27, uint64_t prefab_id) {
  auto fields = parse_owned_fields_or_throw(top27, "prefab top27 removal");
  std::vector<OwnedField> next;
  const std::array<uint32_t, 3> owner_path{4, 50, 502};
  for (auto& field : fields) {
    if (field.number == 1 &&
        field.wire == 2 &&
        read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), owner_path) == prefab_id) {
      continue;
    }
    next.push_back(std::move(field));
  }
  return rebuild_message(next);
}

bool target_ids_contain(const std::set<uint64_t>& target_ids, std::optional<uint64_t> value) {
  return value && target_ids.contains(*value);
}

std::optional<std::vector<OwnedField>> try_parse_owned_message(std::span<const uint8_t> message) {
  std::vector<Field> parsed;
  if (!parse_fields(message, parsed)) return std::nullopt;
  return parse_owned_fields_or_throw(message, "prefab recursive message");
}

bool has_direct_varint_value(std::span<const uint8_t> message, const std::set<uint64_t>& target_ids) {
  const auto fields = try_parse_owned_message(message);
  if (!fields) return false;
  for (const auto& field : *fields) {
    if (field.wire == 0 && target_ids.contains(field.varint)) return true;
  }
  return false;
}

struct RecursivePatch {
  std::vector<uint8_t> data;
  bool changed = false;
};

RecursivePatch strip_mappings_recursive(
    std::span<const uint8_t> message,
    const std::set<uint64_t>& target_ids,
    size_t depth = 0) {
  if (depth > 64) throw std::runtime_error("prefab mapping strip recursion exceeded depth limit");
  auto parsed_fields = try_parse_owned_message(message);
  if (!parsed_fields) return {std::vector<uint8_t>(message.begin(), message.end()), false};
  auto fields = std::move(*parsed_fields);
  std::vector<OwnedField> next;
  bool changed = false;
  const std::array<uint32_t, 1> mapped_id_path{2};

  for (auto& field : fields) {
    if (field.number == 5 &&
        field.wire == 2 &&
        target_ids_contain(target_ids, read_varint_path(std::span<const uint8_t>(field.data.data(), field.data.size()), mapped_id_path))) {
      changed = true;
      continue;
    }

    if (field.wire == 2) {
      auto nested = strip_mappings_recursive(std::span<const uint8_t>(field.data.data(), field.data.size()), target_ids, depth + 1);
      if (nested.changed) {
        field.data = std::move(nested.data);
        changed = true;
      }
    }
    next.push_back(std::move(field));
  }

  RecursivePatch patch;
  patch.changed = changed;
  patch.data = changed ? rebuild_message(next) : std::vector<uint8_t>(message.begin(), message.end());
  return patch;
}

RecursivePatch prune_field10_recursive(
    std::span<const uint8_t> message,
    const std::set<uint64_t>& target_ids,
    size_t depth = 0) {
  if (depth > 64) throw std::runtime_error("prefab top10 prune recursion exceeded depth limit");
  auto parsed_fields = try_parse_owned_message(message);
  if (!parsed_fields) return {std::vector<uint8_t>(message.begin(), message.end()), false};
  auto fields = std::move(*parsed_fields);
  std::vector<OwnedField> next;
  bool changed = false;

  for (auto& field : fields) {
    if (field.wire != 2) {
      next.push_back(std::move(field));
      continue;
    }

    auto nested = prune_field10_recursive(std::span<const uint8_t>(field.data.data(), field.data.size()), target_ids, depth + 1);
    if (nested.changed) {
      field.data = std::move(nested.data);
      changed = true;
    }

    if (has_direct_varint_value(std::span<const uint8_t>(field.data.data(), field.data.size()), target_ids)) {
      changed = true;
      continue;
    }
    next.push_back(std::move(field));
  }

  RecursivePatch patch;
  patch.changed = changed;
  patch.data = changed ? rebuild_message(next) : std::vector<uint8_t>(message.begin(), message.end());
  return patch;
}

void replace_changed_top_field(
    std::vector<uint8_t>& payload_bytes,
    uint32_t top_field_number,
    const std::vector<uint8_t>& before,
    const std::vector<uint8_t>& after,
    std::vector<uint32_t>& changed_top_fields) {
  if (before == after) return;
  payload_bytes = replace_top_level_field_data(payload_bytes, top_field_number, after);
  changed_top_fields.push_back(top_field_number);
}

}  // namespace

PrefabRenameMutation rename_prefab(const GilFile& file, uint64_t prefab_id, const std::string& new_name) {
  const auto top4 = top_level_data(file, 4);
  if (!top4) throw std::runtime_error("top-level field 4 not found");

  RenamePrefabSummary summary;
  summary.prefab_id = prefab_id;
  summary.before_name = find_prefab_name(file, prefab_id);
  summary.after_name = new_name;
  summary.changed_top_fields.push_back(4);

  auto next_top4 = patch_top4_name(*top4, prefab_id, new_name);
  auto next_payload = replace_top_level_field_data(payload(file), 4, next_top4);

  PrefabRenameMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

PrefabCloneMutation clone_prefab_into_tab_by_id(
    const GilFile& file,
    uint64_t source_prefab_id,
    uint64_t target_tab_id,
    const std::string& new_prefab_name,
    const ClonePrefabOptions& options) {
  if (new_prefab_name.empty()) throw std::runtime_error("new prefab name is required");

  const auto top4_span = top_level_data(file, 4);
  const auto top6_span = top_level_data(file, 6);
  if (!top4_span || !top6_span) throw std::runtime_error("required top-level fields 4 and 6 not found");

  const std::vector<uint8_t> top4(top4_span->begin(), top4_span->end());
  const std::vector<uint8_t> top6(top6_span->begin(), top6_span->end());
  std::optional<std::vector<uint8_t>> top27;
  if (const auto top27_span = top_level_data(file, 27)) {
    top27 = std::vector<uint8_t>(top27_span->begin(), top27_span->end());
  }

  std::optional<uint64_t> resolved_tab_id;
  std::string resolved_tab_name;
  for (const auto& tab : list_tabs(file)) {
    if (tab.id && *tab.id == target_tab_id) {
      resolved_tab_id = tab.id;
      resolved_tab_name = tab.name;
      break;
    }
  }
  if (!resolved_tab_id) throw std::runtime_error("target tab id not found");

  const auto source_entry = find_source4_entry(top4, source_prefab_id);
  const auto source_name = read_prefab_name_from_entry(source_entry);
  ensure_prefab_name_does_not_exist(top4, new_prefab_name);

  const auto source27_items = top27 ? find_source27_items(*top27, source_prefab_id) : std::vector<std::vector<uint8_t>>{};
  std::vector<uint64_t> source_decoration_ids;
  source_decoration_ids.reserve(source27_items.size());
  const std::array<uint32_t, 1> decoration_id_path{1};
  for (const auto& item : source27_items) {
    const auto id = read_varint_path(std::span<const uint8_t>(item.data(), item.size()), decoration_id_path);
    if (!id) throw std::runtime_error("source top27 decoration record is missing field 1 id");
    source_decoration_ids.push_back(*id);
  }

  uint64_t new_prefab_id = 0;
  const auto occupied = collect_occupied_object_ids(payload(file), top27);
  if (options.new_prefab_id) {
    if (occupied.contains(*options.new_prefab_id)) throw std::runtime_error("requested new prefab id is already occupied");
    new_prefab_id = *options.new_prefab_id;
  } else {
    new_prefab_id = allocate_prefab_id(payload(file), top27, options.prefab_id_start_after.value_or(source_prefab_id));
  }

  std::vector<uint64_t> used_decoration_ids;
  if (top27) used_decoration_ids = collect_top27_ids(*top27);
  uint64_t next_decoration_id = used_decoration_ids.empty()
      ? 1073741824
      : (*std::max_element(used_decoration_ids.begin(), used_decoration_ids.end()) + 1);
  std::vector<uint64_t> new_decoration_ids;
  new_decoration_ids.reserve(source_decoration_ids.size());
  for (size_t i = 0; i < source_decoration_ids.size(); ++i) {
    new_decoration_ids.push_back(next_decoration_id++);
  }

  const auto [preview_x, preview_z] = compute_next_preview_position(
      payload(file),
      std::span<const uint8_t>(source_entry.data(), source_entry.size()),
      options.preview_x_step,
      options.preview_z_step);

  const auto new4_entry = clone4_entry(
      source_entry,
      new_prefab_id,
      new_prefab_name,
      source_decoration_ids,
      new_decoration_ids,
      preview_x,
      preview_z);

  std::vector<uint8_t> next_payload(payload(file).begin(), payload(file).end());
  next_payload = replace_top_level_field_data(
      next_payload,
      4,
      insert_prefab_entry_before_source(top4, source_prefab_id, new4_entry));

  auto patched_top6 = append_mapping_to_category(top6, resolved_tab_id, "", new_prefab_id);
  patched_top6 = append_unclassified_prefab_mapping(patched_top6, new_prefab_id);
  next_payload = replace_top_level_field_data(next_payload, 6, patched_top6);

  ClonePrefabSummary summary;
  summary.source_prefab_id = source_prefab_id;
  summary.source_name = source_name;
  summary.new_prefab_id = new_prefab_id;
  summary.new_prefab_name = new_prefab_name;
  summary.target_tab_id = resolved_tab_id;
  summary.target_tab_name = resolved_tab_name;
  summary.cloned_decoration_count = source27_items.size();
  summary.preview_x = preview_x;
  summary.preview_z = preview_z;
  summary.changed_top_fields = {4, 6};

  if (top27 && !source27_items.empty()) {
    next_payload = replace_top_level_field_data(
        next_payload,
        27,
        append_cloned_top27_items(*top27, source27_items, new_decoration_ids, new_prefab_id));
    summary.changed_top_fields.push_back(27);
  }

  PrefabCloneMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

PrefabCloneMutation clone_prefab_into_tab(
    const GilFile& file,
    uint64_t source_prefab_id,
    const std::string& target_tab_name,
    const std::string& new_prefab_name,
    const ClonePrefabOptions& options) {
  if (target_tab_name.empty()) throw std::runtime_error("target tab name is required");
  std::optional<uint64_t> target_tab_id;
  for (const auto& tab : list_tabs(file)) {
    if (tab.name == target_tab_name && tab.id) {
      target_tab_id = tab.id;
      break;
    }
  }
  if (!target_tab_id) throw std::runtime_error("target tab name not found");
  return clone_prefab_into_tab_by_id(file, source_prefab_id, *target_tab_id, new_prefab_name, options);
}

PrefabCloneMutation copy_prefab_to_tab(
    const GilFile& file,
    uint64_t source_prefab_id,
    const std::string& target_tab_name,
    const std::optional<std::string>& new_prefab_name,
    const ClonePrefabOptions& options) {
  const auto resolved_name = (new_prefab_name && !new_prefab_name->empty())
      ? *new_prefab_name
      : find_prefab_name(file, source_prefab_id) + "-copy";
  return clone_prefab_into_tab(file, source_prefab_id, target_tab_name, resolved_name, options);
}

PrefabCloneMutation copy_prefab_to_tab_by_id(
    const GilFile& file,
    uint64_t source_prefab_id,
    uint64_t target_tab_id,
    const std::optional<std::string>& new_prefab_name,
    const ClonePrefabOptions& options) {
  const auto resolved_name = (new_prefab_name && !new_prefab_name->empty())
      ? *new_prefab_name
      : find_prefab_name(file, source_prefab_id) + "-copy";
  return clone_prefab_into_tab_by_id(file, source_prefab_id, target_tab_id, resolved_name, options);
}

PrefabDeleteMutation delete_prefab(const GilFile& file, uint64_t prefab_id) {
  const auto top4_span = top_level_data(file, 4);
  if (!top4_span) throw std::runtime_error("top-level field 4 not found");

  std::vector<uint8_t> next_payload(payload(file).begin(), payload(file).end());
  DeletePrefabSummary summary;
  summary.prefab_id = prefab_id;

  const std::vector<uint8_t> top4(top4_span->begin(), top4_span->end());
  bool removed_top4 = false;
  const auto next_top4 = remove_prefab_from_top4(top4, prefab_id, removed_top4);
  if (!removed_top4) throw std::runtime_error("prefab id not found");
  replace_changed_top_field(next_payload, 4, top4, next_top4, summary.changed_top_fields);

  if (const auto top27 = top_level_data_from_payload(next_payload, 27)) {
    summary.removed_decoration_ids = collect_top27_decoration_ids_for_prefab(*top27, prefab_id);
    const auto next_top27 = remove_top27_entries_for_prefab(*top27, prefab_id);
    replace_changed_top_field(next_payload, 27, *top27, next_top27, summary.changed_top_fields);
  }

  std::set<uint64_t> target_ids{prefab_id};
  for (uint64_t decoration_id : summary.removed_decoration_ids) target_ids.insert(decoration_id);

  if (const auto top6 = top_level_data_from_payload(next_payload, 6)) {
    const auto patch = strip_mappings_recursive(*top6, target_ids);
    replace_changed_top_field(next_payload, 6, *top6, patch.data, summary.changed_top_fields);
  }

  if (const auto top10 = top_level_data_from_payload(next_payload, 10)) {
    const auto patch = prune_field10_recursive(*top10, target_ids);
    replace_changed_top_field(next_payload, 10, *top10, patch.data, summary.changed_top_fields);
  }

  PrefabDeleteMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

std::string rename_prefab_summary_to_json(const RenamePrefabSummary& summary) {
  std::ostringstream out;
  out << "{"
      << "\"prefabId\":" << summary.prefab_id << ","
      << "\"beforeName\":" << json::quote(summary.before_name) << ","
      << "\"afterName\":" << json::quote(summary.after_name) << ","
      << "\"changedTopFields\":[";
  for (size_t i = 0; i < summary.changed_top_fields.size(); ++i) {
    if (i) out << ",";
    out << summary.changed_top_fields[i];
  }
  out << "]}";
  return out.str();
}

std::string delete_prefab_summary_to_json(const DeletePrefabSummary& summary) {
  std::ostringstream out;
  out << "{"
      << "\"kind\":\"deletePrefab\","
      << "\"prefabId\":" << summary.prefab_id << ","
      << "\"removedDecorationIds\":[";
  for (size_t i = 0; i < summary.removed_decoration_ids.size(); ++i) {
    if (i) out << ",";
    out << summary.removed_decoration_ids[i];
  }
  out << "],\"changedTopFields\":[";
  for (size_t i = 0; i < summary.changed_top_fields.size(); ++i) {
    if (i) out << ",";
    out << summary.changed_top_fields[i];
  }
  out << "]}";
  return out.str();
}

std::string clone_prefab_summary_to_json_with_kind(const ClonePrefabSummary& summary, std::string_view kind) {
  std::ostringstream preview_x;
  preview_x << std::fixed << std::setprecision(6) << summary.preview_x;
  std::ostringstream preview_z;
  preview_z << std::fixed << std::setprecision(6) << summary.preview_z;

  std::ostringstream out;
  out << "{"
      << "\"kind\":" << json::quote(std::string(kind)) << ","
      << "\"sourcePrefabId\":" << summary.source_prefab_id << ","
      << "\"sourceName\":" << json::quote(summary.source_name) << ","
      << "\"newPrefabId\":" << summary.new_prefab_id << ","
      << "\"newPrefabName\":" << json::quote(summary.new_prefab_name) << ","
      << "\"targetTab\":{"
      << "\"id\":" << (summary.target_tab_id ? json::number(*summary.target_tab_id) : "null") << ","
      << "\"name\":" << json::quote(summary.target_tab_name)
      << "},"
      << "\"clonedDecorationCount\":" << summary.cloned_decoration_count << ","
      << "\"previewPos\":{"
      << "\"x\":" << preview_x.str() << ","
      << "\"z\":" << preview_z.str()
      << "},"
      << "\"changedTopFields\":[";
  for (size_t i = 0; i < summary.changed_top_fields.size(); ++i) {
    if (i) out << ",";
    out << summary.changed_top_fields[i];
  }
  out << "]}";
  return out.str();
}

std::string clone_prefab_summary_to_json(const ClonePrefabSummary& summary) {
  return clone_prefab_summary_to_json_with_kind(summary, "clonePrefab");
}

std::string copy_prefab_summary_to_json(const ClonePrefabSummary& summary) {
  return clone_prefab_summary_to_json_with_kind(summary, "copyPrefabToTab");
}

}  // namespace opengil
