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

OwnedField len_field(uint32_t number, std::vector<uint8_t> data) {
  OwnedField field;
  field.number = number;
  field.wire = 2;
  field.data = std::move(data);
  return field;
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

}  // namespace

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

}  // namespace opengil
