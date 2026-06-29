#include "opengil/ui_structure_ops.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "opengil/wire.hpp"

namespace opengil {
namespace {

struct Top9Entry {
  size_t top9_index = 0;
  uint64_t entry_id = 0;
  std::vector<uint8_t> data;
};

template <size_t N>
std::optional<uint64_t> read_varint_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_varint_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

std::span<const uint8_t> bytes_span(const std::vector<uint8_t>& bytes) {
  return std::span<const uint8_t>(bytes.data(), bytes.size());
}

std::optional<std::vector<uint8_t>> direct_len_data(std::span<const uint8_t> message, uint32_t field_number) {
  const auto field = first_len_field(message, field_number);
  if (!field) return std::nullopt;
  const auto data = field_data(message, *field);
  return std::vector<uint8_t>(data.begin(), data.end());
}

bool is_ui_primitive_type(uint64_t value) {
  return value == kUiPrimitiveRectangle ||
         value == kUiPrimitiveEllipse ||
         value == kUiPrimitiveTriangle;
}

bool is_ui_primitive_message(std::span<const uint8_t> message) {
  const auto primitive31 = direct_len_data(message, 31);
  if (!primitive31) return false;
  const std::array<uint32_t, 1> type_path{2};
  const auto type_id = read_varint_path(bytes_span(*primitive31), type_path);
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

std::optional<uint64_t> entry_id_from_top9_entry(std::span<const uint8_t> entry) {
  const std::array<uint32_t, 1> id_path{501};
  return read_varint_path(entry, id_path);
}

std::vector<uint64_t> decode_packed_varints(std::span<const uint8_t> data) {
  std::vector<uint64_t> values;
  size_t offset = 0;
  while (offset < data.size()) {
    const auto value = read_varint(data, offset);
    if (!value || value->next <= offset) {
      throw std::runtime_error("packed varint list parse failed");
    }
    values.push_back(value->value);
    offset = value->next;
  }
  return values;
}

std::vector<uint8_t> encode_packed_varints(const std::vector<uint64_t>& values) {
  std::vector<uint8_t> out;
  for (uint64_t value : values) {
    auto encoded = encode_varint(value);
    out.insert(out.end(), encoded.begin(), encoded.end());
  }
  return out;
}

std::vector<Top9Entry> top9_entries(std::span<const uint8_t> top9) {
  std::vector<Field> fields;
  if (!parse_fields(top9, fields)) throw std::runtime_error("top-level field 9 parse failed");

  std::vector<Top9Entry> entries;
  for (size_t i = 0; i < fields.size(); ++i) {
    const auto& field = fields[i];
    if (field.number != 502 || field.wire != 2) continue;
    const auto data = field_data(top9, field);
    const auto entry_id = entry_id_from_top9_entry(data);
    if (!entry_id) continue;

    Top9Entry entry;
    entry.top9_index = i;
    entry.entry_id = *entry_id;
    entry.data.assign(data.begin(), data.end());
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::vector<Top9Entry> primitive_entries(std::span<const uint8_t> top9) {
  std::vector<Top9Entry> primitives;
  for (auto entry : top9_entries(top9)) {
    if (contains_ui_primitive_message(bytes_span(entry.data))) {
      primitives.push_back(std::move(entry));
    }
  }
  return primitives;
}

const Top9Entry* find_entry_by_id(const std::vector<Top9Entry>& entries, uint64_t entry_id) {
  for (const auto& entry : entries) {
    if (entry.entry_id == entry_id) return &entry;
  }
  return nullptr;
}

Top9Entry controller_entry(std::span<const uint8_t> top9, uint64_t controller_entry_id) {
  const auto entries = top9_entries(top9);
  const auto* entry = find_entry_by_id(entries, controller_entry_id);
  if (!entry) throw std::runtime_error("ui primitive controller not found");
  return *entry;
}

std::vector<uint64_t> controller_child_ids(std::span<const uint8_t> top9, uint64_t controller_entry_id) {
  const auto controller = controller_entry(top9, controller_entry_id);
  const auto packed = direct_len_data(bytes_span(controller.data), 503);
  if (!packed) throw std::runtime_error("ui primitive controller child list not found");
  return decode_packed_varints(bytes_span(*packed));
}

std::vector<Top9Entry> ordered_primitives(std::span<const uint8_t> top9, uint64_t controller_entry_id) {
  const auto primitives = primitive_entries(top9);
  const auto ids = controller_child_ids(top9, controller_entry_id);
  std::vector<Top9Entry> ordered;
  ordered.reserve(ids.size());
  for (uint64_t id : ids) {
    const auto* entry = find_entry_by_id(primitives, id);
    if (!entry) throw std::runtime_error("controller child primitive entry not found");
    ordered.push_back(*entry);
  }
  return ordered;
}

Top9Entry primitive_by_index(std::span<const uint8_t> top9, size_t primitive_index, uint64_t controller_entry_id) {
  const auto entries = ordered_primitives(top9, controller_entry_id);
  if (primitive_index >= entries.size()) throw std::runtime_error("ui primitive not found");
  return entries[primitive_index];
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

std::vector<uint8_t> set_direct_varint_field(std::span<const uint8_t> message, uint32_t field_number, uint64_t value) {
  auto fields = parse_owned_fields_or_throw(message, "ui message");
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

std::vector<uint8_t> set_direct_len_field(std::span<const uint8_t> message, uint32_t field_number, std::vector<uint8_t> data) {
  auto fields = parse_owned_fields_or_throw(message, "ui message");
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

std::optional<std::vector<uint8_t>> try_replace_varint_value_recursive(
    std::span<const uint8_t> message,
    uint64_t old_value,
    uint64_t new_value,
    size_t depth) {
  if (depth > 64) throw std::runtime_error("ui recursive replace exceeded depth limit");
  std::vector<Field> parsed;
  if (!parse_fields(message, parsed)) return std::nullopt;

  auto fields = parse_owned_fields_or_throw(message, "ui recursive replace");
  for (auto& field : fields) {
    if (field.wire == 0 && field.varint == old_value) {
      field.varint = new_value;
    } else if (field.wire == 2) {
      if (auto replaced = try_replace_varint_value_recursive(bytes_span(field.data), old_value, new_value, depth + 1)) {
        field.data = std::move(*replaced);
      }
    }
  }
  return rebuild_message(fields);
}

std::vector<uint8_t> replace_varint_value_recursive(
    std::span<const uint8_t> message,
    uint64_t old_value,
    uint64_t new_value) {
  auto replaced = try_replace_varint_value_recursive(message, old_value, new_value, 0);
  if (!replaced) throw std::runtime_error("ui recursive replace parse failed");
  return *replaced;
}

std::vector<uint8_t> clone_primitive_entry(
    const Top9Entry& template_entry,
    uint64_t new_entry_id,
    uint64_t target_controller_entry_id) {
  auto next = replace_varint_value_recursive(bytes_span(template_entry.data), template_entry.entry_id, new_entry_id);
  next = set_direct_varint_field(bytes_span(next), 501, new_entry_id);
  next = set_direct_varint_field(bytes_span(next), 504, target_controller_entry_id);
  return next;
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
  const auto controller = controller_entry(top9, controller_entry_id);
  const auto next_controller = set_direct_len_field(bytes_span(controller.data), 503, encode_packed_varints(entry_ids));
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

std::vector<uint8_t> remove_unretained_primitives(
    std::span<const uint8_t> top9,
    uint64_t controller_entry_id,
    const std::vector<uint64_t>& keep_entry_ids) {
  const auto primitives = primitive_entries(top9);
  std::unordered_set<uint64_t> primitive_ids;
  for (const auto& primitive : primitives) primitive_ids.insert(primitive.entry_id);

  std::unordered_set<uint64_t> keep_ids(keep_entry_ids.begin(), keep_entry_ids.end());
  auto fields = parse_owned_fields_or_throw(top9, "top-level field 9");
  std::vector<OwnedField> retained;
  retained.reserve(fields.size());

  for (auto& field : fields) {
    if (field.number != 502 || field.wire != 2) {
      retained.push_back(std::move(field));
      continue;
    }

    const auto entry_id = entry_id_from_top9_entry(bytes_span(field.data));
    if (entry_id && *entry_id == controller_entry_id) {
      field.data = set_direct_len_field(bytes_span(field.data), 503, encode_packed_varints(keep_entry_ids));
      retained.push_back(std::move(field));
      continue;
    }

    if (entry_id && primitive_ids.contains(*entry_id) && !keep_ids.contains(*entry_id)) continue;
    retained.push_back(std::move(field));
  }

  return rebuild_message(retained);
}

std::vector<uint8_t> replace_primitive_by_index(
    std::span<const uint8_t> top9,
    uint64_t controller_entry_id,
    size_t primitive_index,
    const std::vector<uint8_t>& new_entry_data) {
  const auto target = primitive_by_index(top9, primitive_index, controller_entry_id);
  return replace_top9_entry_by_id(top9, target.entry_id, new_entry_data);
}

GilFile file_from_bytes(const GilFile& base, const std::vector<uint8_t>& bytes) {
  GilFile file;
  file.path = base.path;
  file.header = base.header;
  file.bytes = bytes;
  return file;
}

UiStructureMutation make_mutation(
    const GilFile& file,
    std::vector<uint8_t> next_payload,
    UiStructureSummary summary) {
  UiStructureMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

std::vector<uint64_t> listed_entry_ids(const GilFile& file, uint64_t controller_entry_id) {
  const auto list = list_ui_primitives(file, controller_entry_id);
  std::vector<uint64_t> ids;
  ids.reserve(list.primitives.size());
  for (const auto& primitive : list.primitives) {
    if (primitive.entry_id) ids.push_back(*primitive.entry_id);
  }
  return ids;
}

UiStructureSummary summary_for_result(
    std::string kind,
    const GilFile& result_file,
    uint64_t controller_entry_id) {
  UiStructureSummary summary;
  summary.kind = std::move(kind);
  summary.target_controller_entry_id = controller_entry_id;
  summary.entry_ids = listed_entry_ids(result_file, controller_entry_id);
  summary.primitive_count = summary.entry_ids.size();
  summary.changed_top_fields = {9};
  return summary;
}

std::optional<uint64_t> direct_controller_id(const Top9Entry& primitive) {
  const std::array<uint32_t, 1> path{504};
  return read_varint_path(bytes_span(primitive.data), path);
}

}  // namespace

UiStructureMutation append_ui_primitive_from_template(
    const GilFile& file,
    const GilFile& template_file,
    const UiAppendOptions& options) {
  const auto top9 = top_level_data(file, 9);
  const auto template_top9 = top_level_data(template_file, 9);
  if (!top9) throw std::runtime_error("top-level field 9 not found");
  if (!template_top9) throw std::runtime_error("template top-level field 9 not found");

  const auto template_primitives = primitive_entries(*template_top9);
  if (options.template_primitive_index >= template_primitives.size()) {
    throw std::runtime_error("template ui primitive not found");
  }
  const auto& template_primitive = template_primitives[options.template_primitive_index];
  const auto template_controller_id = direct_controller_id(template_primitive);
  const uint64_t controller_id = options.target_controller_entry_id
      ? *options.target_controller_entry_id
      : template_controller_id.value_or(kDefaultUiPrimitiveControllerEntryId);
  controller_entry(*top9, controller_id);

  const auto used_ids = used_entry_ids(*top9);
  const uint64_t new_entry_id = options.entry_id.value_or(
      used_ids.contains(template_primitive.entry_id) ? allocate_top9_entry_id(*top9) : template_primitive.entry_id);
  if (used_ids.contains(new_entry_id)) throw std::runtime_error("ui primitive entry id already exists");

  const auto new_entry = clone_primitive_entry(template_primitive, new_entry_id, controller_id);
  auto next_top9 = append_controller_child_ids(*top9, controller_id, {new_entry_id});
  next_top9 = insert_top9_entries_before(bytes_span(next_top9), {new_entry}, 1073741841);

  auto next_payload = replace_top_level_field_data(payload(file), 9, next_top9);
  auto result_file = file_from_bytes(file, build_gil_bytes(file.header, next_payload));
  auto summary = summary_for_result("appendUiPrimitive", result_file, controller_id);
  return make_mutation(file, std::move(next_payload), std::move(summary));
}

UiStructureMutation retain_ui_primitives(
    const GilFile& file,
    const std::vector<size_t>& primitive_indexes,
    const UiRetainOptions& options) {
  const auto top9 = top_level_data(file, 9);
  if (!top9) throw std::runtime_error("top-level field 9 not found");

  const uint64_t controller_id = options.target_controller_entry_id.value_or(kDefaultUiPrimitiveControllerEntryId);
  const auto ordered = ordered_primitives(*top9, controller_id);
  std::vector<uint64_t> keep_entry_ids;
  keep_entry_ids.reserve(primitive_indexes.size());
  for (size_t index : primitive_indexes) {
    if (index >= ordered.size()) throw std::runtime_error("ui primitive not found");
    keep_entry_ids.push_back(ordered[index].entry_id);
  }

  const auto next_top9 = remove_unretained_primitives(*top9, controller_id, keep_entry_ids);
  auto next_payload = replace_top_level_field_data(payload(file), 9, next_top9);
  auto result_file = file_from_bytes(file, build_gil_bytes(file.header, next_payload));
  auto summary = summary_for_result("retainUiPrimitives", result_file, controller_id);
  return make_mutation(file, std::move(next_payload), std::move(summary));
}

UiStructureMutation copy_ui_primitive_transform_from_template(
    const GilFile& file,
    const GilFile& template_file,
    const UiCopyTransformFromTemplateOptions& options) {
  const auto top9 = top_level_data(file, 9);
  const auto template_top9 = top_level_data(template_file, 9);
  if (!top9) throw std::runtime_error("top-level field 9 not found");
  if (!template_top9) throw std::runtime_error("template top-level field 9 not found");

  const uint64_t target_controller_id = options.target_controller_entry_id.value_or(kDefaultUiPrimitiveControllerEntryId);
  const auto target = primitive_by_index(*top9, options.primitive_index, target_controller_id);
  const auto template_primitives = primitive_entries(*template_top9);
  if (options.template_primitive_index >= template_primitives.size()) {
    throw std::runtime_error("template ui primitive not found");
  }
  const auto& template_primitive = template_primitives[options.template_primitive_index];
  const uint64_t controller_id = direct_controller_id(target).value_or(kDefaultUiPrimitiveControllerEntryId);

  auto next_entry = replace_varint_value_recursive(bytes_span(template_primitive.data), template_primitive.entry_id, target.entry_id);
  next_entry = set_direct_varint_field(bytes_span(next_entry), 501, target.entry_id);
  next_entry = set_direct_varint_field(bytes_span(next_entry), 504, controller_id);

  const auto next_top9 = replace_primitive_by_index(*top9, target_controller_id, options.primitive_index, next_entry);
  auto next_payload = replace_top_level_field_data(payload(file), 9, next_top9);
  auto result_file = file_from_bytes(file, build_gil_bytes(file.header, next_payload));
  auto summary = summary_for_result("copyUiPrimitiveTransformFromTemplate", result_file, target_controller_id);
  summary.entry_ids = {target.entry_id};
  return make_mutation(file, std::move(next_payload), std::move(summary));
}

}  // namespace opengil
