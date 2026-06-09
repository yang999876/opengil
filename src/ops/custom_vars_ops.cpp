#include "opengil/custom_vars_ops.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "opengil/json.hpp"
#include "opengil/semantic.hpp"

namespace opengil {
namespace {

struct CustomTypeSpec {
  uint64_t type_id = 0;
  uint32_t marker_field = 0;
  std::string name;
};

struct SyncCounts {
  size_t prefab_count = 0;
  size_t scene_count = 0;
  size_t preview_count = 0;
  std::vector<uint32_t> changed_top_fields;
};

const std::map<std::string, CustomTypeSpec>& type_specs_by_name() {
  static const std::map<std::string, CustomTypeSpec> specs = {
      {"entity", {1, 11, "entity"}},
      {"int", {3, 13, "int"}},
      {"bool", {4, 14, "bool"}},
      {"float", {5, 15, "float"}},
      {"str", {6, 16, "str"}},
      {"string", {6, 16, "str"}},
      {"vec", {12, 22, "vec"}},
      {"vec3", {12, 22, "vec"}},
  };
  return specs;
}

std::string type_name_from_id(uint64_t type_id) {
  switch (type_id) {
    case 1:
      return "entity";
    case 3:
      return "int";
    case 4:
      return "bool";
    case 5:
      return "float";
    case 6:
      return "str";
    case 12:
      return "vec";
    default: {
      std::ostringstream out;
      out << "typeId:" << type_id;
      return out.str();
    }
  }
}

CustomTypeSpec normalize_type(const std::string& type) {
  std::string key;
  key.reserve(type.size());
  for (unsigned char ch : type) {
    key.push_back(static_cast<char>(std::tolower(ch)));
  }
  const auto it = type_specs_by_name().find(key);
  if (it == type_specs_by_name().end()) {
    throw std::runtime_error("unsupported custom variable type: " + type);
  }
  return it->second;
}

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

std::vector<uint8_t> bytes_from_string(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> build_value_b(uint64_t type_id) {
  return rebuild_message({
      make_varint_field(1, type_id),
      make_len_field(2, {}),
  });
}

std::vector<uint8_t> build_value_a(const CustomTypeSpec& type) {
  std::vector<OwnedField> fields;
  fields.push_back(make_varint_field(1, type.type_id));
  fields.push_back(make_len_field(2, build_value_b(type.type_id)));
  if (type.marker_field == 22) {
    fields.push_back(make_len_field(22, rebuild_message({make_len_field(1, {})})));
  } else {
    fields.push_back(make_len_field(type.marker_field, {}));
  }
  return rebuild_message(fields);
}

std::vector<uint8_t> build_custom_variable_entry(const std::string& name, const std::string& type_name) {
  const auto type = normalize_type(type_name);
  return rebuild_message({
      make_len_field(2, bytes_from_string(name)),
      make_varint_field(3, type.type_id),
      make_len_field(4, build_value_a(type)),
      make_varint_field(5, 1),
      make_len_field(6, build_value_b(type.type_id)),
  });
}

std::vector<uint8_t> build_empty_custom_variable_component() {
  return rebuild_message({
      make_varint_field(1, 1),
      make_varint_field(2, 1),
      make_len_field(11, {}),
  });
}

std::string read_prefab_name_from_entry(std::span<const uint8_t> entry) {
  const std::array<uint32_t, 3> preferred{6, 11, 1};
  if (auto name = read_string_path(entry, preferred)) return normalize_visible_text(*name);
  const std::array<uint32_t, 2> fallback1{6, 11};
  if (auto name = read_string_path(entry, fallback1)) return normalize_visible_text(*name);
  const std::array<uint32_t, 1> fallback2{3};
  if (auto name = read_string_path(entry, fallback2)) return normalize_visible_text(*name);
  return "";
}

int find_custom_variable_component_index(const std::vector<OwnedField>& entry_fields, uint32_t component_field_no) {
  for (size_t i = 0; i < entry_fields.size(); ++i) {
    const auto& field = entry_fields[i];
    if (field.number != component_field_no || field.wire != 2) continue;
    const auto component_fields = parse_owned_fields(field.data);
    if (std::any_of(component_fields.begin(), component_fields.end(), [](const OwnedField& child) {
          return child.number == 11 && child.wire == 2;
        })) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::vector<std::vector<uint8_t>> get_custom_variable_entries(std::span<const uint8_t> component) {
  std::vector<std::vector<uint8_t>> entries;
  const auto component_fields = parse_owned_fields(component);
  for (const auto& field : component_fields) {
    if (field.number != 11 || field.wire != 2) continue;
    for (const auto& entry_field : len_fields(field.data, 1)) {
      const auto data = field_data(field.data, entry_field);
      entries.emplace_back(data.begin(), data.end());
    }
    break;
  }
  return entries;
}

CustomVariableInfo parse_custom_variable_entry(std::span<const uint8_t> entry) {
  CustomVariableInfo info;
  const std::array<uint32_t, 1> name_path{2};
  if (auto name = read_string_path(entry, name_path)) info.name = *name;
  const std::array<uint32_t, 1> type_path{3};
  info.type_id = read_varint_path(entry, type_path).value_or(0);
  info.type = type_name_from_id(info.type_id);
  const std::array<uint32_t, 1> enabled_path{5};
  info.enabled = read_varint_path(entry, enabled_path);
  return info;
}

std::vector<uint8_t> set_custom_variable_entries(
    std::span<const uint8_t> component,
    const std::vector<std::vector<uint8_t>>& entries) {
  auto component_fields = parse_owned_fields(component);
  std::vector<OwnedField> entry_fields;
  entry_fields.reserve(entries.size());
  for (const auto& entry : entries) entry_fields.push_back(make_len_field(1, entry));
  const auto entry_payload = rebuild_message(entry_fields);

  bool changed = false;
  for (auto& field : component_fields) {
    if (field.number == 11 && field.wire == 2) {
      field.data = entry_payload;
      changed = true;
      break;
    }
  }
  if (!changed) component_fields.push_back(make_len_field(11, entry_payload));
  return rebuild_message(component_fields);
}

using EntryList = std::vector<std::vector<uint8_t>>;
using EntryPatcher = std::function<EntryList(const EntryList&)>;

GilFile file_from_payload(std::span<const uint8_t> payload_bytes);

std::vector<uint8_t> patch_entry_custom_variables(
    std::span<const uint8_t> entry,
    uint32_t component_field_no,
    const EntryPatcher& patcher) {
  auto entry_fields = parse_owned_fields(entry);
  int component_index = find_custom_variable_component_index(entry_fields, component_field_no);
  if (component_index < 0) {
    entry_fields.push_back(make_len_field(component_field_no, build_empty_custom_variable_component()));
    component_index = static_cast<int>(entry_fields.size() - 1);
  }

  auto& component = entry_fields[static_cast<size_t>(component_index)];
  const auto current_entries = get_custom_variable_entries(
      std::span<const uint8_t>(component.data.data(), component.data.size()));
  const auto next_entries = patcher(current_entries);
  component.data = set_custom_variable_entries(
      std::span<const uint8_t>(component.data.data(), component.data.size()),
      next_entries);
  return rebuild_message(entry_fields);
}

SyncCounts patch_across_spaces(
    std::vector<uint8_t>& next_payload,
    uint64_t prefab_id,
    const EntryPatcher& patcher) {
  SyncCounts summary;

  const auto top4_file = file_from_payload(next_payload);
  const auto top4 = top_level_data(top4_file, 4);
  if (!top4) throw std::runtime_error("top-level field 4 not found");
  auto top4_fields = parse_owned_fields(*top4);
  bool prefab_changed = false;
  for (auto& field : top4_fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto entry = std::span<const uint8_t>(field.data.data(), field.data.size());
    const std::array<uint32_t, 1> id_path{1};
    if (read_varint_path(entry, id_path) != prefab_id) continue;
    field.data = patch_entry_custom_variables(entry, 8, patcher);
    prefab_changed = true;
    summary.prefab_count++;
    break;
  }
  if (!prefab_changed) throw std::runtime_error("prefab id not found");
  const auto next_top4 = rebuild_message(top4_fields);
  if (next_top4 != std::vector<uint8_t>(top4->begin(), top4->end())) {
    next_payload = replace_top_level_field_data(next_payload, 4, next_top4);
    summary.changed_top_fields.push_back(4);
  }

  auto patch_linked_top = [&](uint32_t top_field_no, uint32_t component_field_no, size_t& count) {
    const auto temp_file = file_from_payload(next_payload);
    const auto top = top_level_data(temp_file, top_field_no);
    if (!top) return;
    auto top_fields = parse_owned_fields(*top);
    bool changed = false;
    for (auto& field : top_fields) {
      if (field.number != 1 || field.wire != 2) continue;
      const auto entry = std::span<const uint8_t>(field.data.data(), field.data.size());
      const std::array<uint32_t, 2> ref_path{2, 1};
      if (read_varint_path(entry, ref_path) != prefab_id) continue;
      field.data = patch_entry_custom_variables(entry, component_field_no, patcher);
      changed = true;
      count++;
    }
    if (changed) {
      const auto next_top = rebuild_message(top_fields);
      if (next_top != std::vector<uint8_t>(top->begin(), top->end())) {
        next_payload = replace_top_level_field_data(next_payload, top_field_no, next_top);
        summary.changed_top_fields.push_back(top_field_no);
      }
    }
  };

  patch_linked_top(5, 7, summary.scene_count);
  patch_linked_top(8, 7, summary.preview_count);
  return summary;
}

std::optional<std::vector<uint8_t>> find_prefab_entry_data(const GilFile& file, uint64_t prefab_id) {
  const auto top4 = top_level_data(file, 4);
  if (!top4) return std::nullopt;
  for (const auto& field : len_fields(*top4, 1)) {
    const auto entry = field_data(*top4, field);
    const std::array<uint32_t, 1> id_path{1};
    if (read_varint_path(entry, id_path) == prefab_id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  return std::nullopt;
}

std::vector<std::vector<uint8_t>> raw_custom_entries_from_prefab(const GilFile& file, uint64_t prefab_id) {
  const auto entry = find_prefab_entry_data(file, prefab_id);
  if (!entry) throw std::runtime_error("source prefab id not found");
  const auto entry_fields = parse_owned_fields(*entry);
  const int component_index = find_custom_variable_component_index(entry_fields, 8);
  if (component_index < 0) return {};
  const auto& component = entry_fields[static_cast<size_t>(component_index)];
  return get_custom_variable_entries(std::span<const uint8_t>(component.data.data(), component.data.size()));
}

std::string sync_counts_json(const SyncCounts& counts) {
  std::ostringstream out;
  out << "{"
      << "\"prefabCount\":" << counts.prefab_count << ","
      << "\"sceneCount\":" << counts.scene_count << ","
      << "\"previewCount\":" << counts.preview_count
      << "}";
  return out.str();
}

std::string changed_fields_json(const std::vector<uint32_t>& fields) {
  return json::array_of_numbers(fields);
}

GilFile file_from_bytes(const GilFile& file, const std::vector<uint8_t>& bytes) {
  GilFile next = file;
  next.bytes = bytes;
  return next;
}

GilFile file_from_payload(std::span<const uint8_t> payload_bytes) {
  GilFile file;
  file.bytes = build_gil_bytes(file.header, payload_bytes);
  return file;
}

CustomVarsMutation finish_mutation(
    const GilFile& file,
    std::vector<uint8_t> payload_bytes,
    std::string result_json,
    std::vector<uint32_t> changed_top_fields) {
  CustomVarsMutation mutation;
  mutation.payload = std::move(payload_bytes);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.result_json = std::move(result_json);
  mutation.changed_top_fields = std::move(changed_top_fields);
  return mutation;
}

std::string prefab_name_by_id(const GilFile& file, uint64_t prefab_id) {
  for (const auto& prefab : list_prefabs(file)) {
    if (prefab.prefab_id == prefab_id) return prefab.name;
  }
  return "";
}

}  // namespace

std::vector<PrefabCustomVariables> list_prefab_custom_variables(
    const GilFile& file,
    std::optional<uint64_t> prefab_id) {
  std::vector<PrefabCustomVariables> rows;
  const auto top4 = top_level_data(file, 4);
  if (!top4) throw std::runtime_error("top-level field 4 not found");

  for (const auto& field : len_fields(*top4, 1)) {
    const auto entry = field_data(*top4, field);
    const std::array<uint32_t, 1> id_path{1};
    const auto id = read_varint_path(entry, id_path);
    if (!id) continue;
    if (prefab_id && *prefab_id != *id) continue;

    PrefabCustomVariables row;
    row.prefab_id = *id;
    row.prefab_name = read_prefab_name_from_entry(entry);

    const auto entry_fields = parse_owned_fields(entry);
    const int component_index = find_custom_variable_component_index(entry_fields, 8);
    if (component_index >= 0) {
      const auto& component = entry_fields[static_cast<size_t>(component_index)];
      for (const auto& variable_entry : get_custom_variable_entries(
               std::span<const uint8_t>(component.data.data(), component.data.size()))) {
        row.variables.push_back(parse_custom_variable_entry(variable_entry));
      }
    }
    rows.push_back(std::move(row));
  }

  return rows;
}

CustomVarsMutation add_prefab_custom_variable(
    const GilFile& file,
    uint64_t prefab_id,
    const std::string& name,
    const std::string& type) {
  if (name.empty()) throw std::runtime_error("custom variable name is required");
  const auto new_entry = build_custom_variable_entry(name, type);
  std::vector<uint8_t> next_payload(payload(file).begin(), payload(file).end());
  const auto sync = patch_across_spaces(next_payload, prefab_id, [&](const EntryList& entries) {
    for (const auto& entry : entries) {
      if (parse_custom_variable_entry(entry).name == name) {
        throw std::runtime_error("custom variable already exists: " + name);
      }
    }
    EntryList next = entries;
    next.push_back(new_entry);
    return next;
  });

  const auto spec = normalize_type(type);
  std::ostringstream result;
  result << "{"
         << "\"kind\":\"customVarsAdd\","
         << "\"prefabId\":" << prefab_id << ","
         << "\"prefabName\":" << json::quote(prefab_name_by_id(file, prefab_id)) << ","
         << "\"variable\":{"
         << "\"name\":" << json::quote(name) << ","
         << "\"typeId\":" << spec.type_id << ","
         << "\"type\":" << json::quote(spec.name)
         << "},\"synchronized\":" << sync_counts_json(sync) << ","
         << "\"changedTopFields\":" << changed_fields_json(sync.changed_top_fields)
         << "}";
  return finish_mutation(file, std::move(next_payload), result.str(), sync.changed_top_fields);
}

CustomVarsMutation remove_prefab_custom_variable(
    const GilFile& file,
    uint64_t prefab_id,
    const std::string& name) {
  CustomVariableInfo removed;
  bool removed_any = false;
  std::vector<uint8_t> next_payload(payload(file).begin(), payload(file).end());
  const auto sync = patch_across_spaces(next_payload, prefab_id, [&](const EntryList& entries) {
    EntryList next;
    bool removed_here = false;
    for (const auto& entry : entries) {
      const auto parsed = parse_custom_variable_entry(entry);
      if (!removed_here && parsed.name == name) {
        removed = parsed;
        removed_any = true;
        removed_here = true;
        continue;
      }
      next.push_back(entry);
    }
    if (!removed_here) throw std::runtime_error("custom variable not found: " + name);
    return next;
  });
  if (!removed_any) throw std::runtime_error("custom variable not found: " + name);

  std::ostringstream result;
  result << "{"
         << "\"kind\":\"customVarsRemove\","
         << "\"prefabId\":" << prefab_id << ","
         << "\"prefabName\":" << json::quote(prefab_name_by_id(file, prefab_id)) << ","
         << "\"variable\":{"
         << "\"name\":" << json::quote(removed.name) << ","
         << "\"typeId\":" << removed.type_id << ","
         << "\"type\":" << json::quote(removed.type)
         << "},\"synchronized\":" << sync_counts_json(sync) << ","
         << "\"changedTopFields\":" << changed_fields_json(sync.changed_top_fields)
         << "}";
  return finish_mutation(file, std::move(next_payload), result.str(), sync.changed_top_fields);
}

CustomVarsMutation copy_prefab_custom_variables(
    const GilFile& file,
    uint64_t source_prefab_id,
    uint64_t target_prefab_id) {
  const auto source_entries = raw_custom_entries_from_prefab(file, source_prefab_id);
  std::vector<uint8_t> next_payload(payload(file).begin(), payload(file).end());
  const auto sync = patch_across_spaces(next_payload, target_prefab_id, [&](const EntryList&) {
    return source_entries;
  });

  std::ostringstream result;
  result << "{"
         << "\"kind\":\"customVarsCopyAll\","
         << "\"sourcePrefabId\":" << source_prefab_id << ","
         << "\"sourcePrefabName\":" << json::quote(prefab_name_by_id(file, source_prefab_id)) << ","
         << "\"targetPrefabId\":" << target_prefab_id << ","
         << "\"targetPrefabName\":" << json::quote(prefab_name_by_id(file, target_prefab_id)) << ","
         << "\"variableCount\":" << source_entries.size() << ","
         << "\"synchronized\":" << sync_counts_json(sync) << ","
         << "\"changedTopFields\":" << changed_fields_json(sync.changed_top_fields)
         << "}";
  return finish_mutation(file, std::move(next_payload), result.str(), sync.changed_top_fields);
}

CustomVarsMutation sync_tab_custom_variables_impl(
    const GilFile& file,
    uint64_t source_prefab_id,
    const std::string& tab_label,
    const std::function<bool(const TabInfo&)>& tab_matcher) {
  const auto source_entries = raw_custom_entries_from_prefab(file, source_prefab_id);
  GilFile current = file;
  std::vector<uint8_t> final_bytes = file.bytes;
  std::vector<std::string> item_jsons;
  std::set<uint32_t> changed_fields;

  std::set<uint64_t> target_ids;
  for (const auto& tab : list_tabs(file)) {
    if (tab_matcher(tab)) target_ids.insert(tab.prefab_ids.begin(), tab.prefab_ids.end());
  }
  std::vector<PrefabInfo> targets;
  for (const auto& prefab : list_prefabs(file)) {
    if (target_ids.contains(prefab.prefab_id)) targets.push_back(prefab);
  }

  for (const auto& target : targets) {
    const auto mutation = copy_prefab_custom_variables(current, source_prefab_id, target.prefab_id);
    final_bytes = mutation.bytes;
    current = file_from_bytes(file, final_bytes);
    for (uint32_t field : mutation.changed_top_fields) changed_fields.insert(field);
    item_jsons.push_back(mutation.result_json);
  }

  std::ostringstream changed_json;
  changed_json << "[";
  size_t idx = 0;
  for (uint32_t field : changed_fields) {
    if (idx++) changed_json << ",";
    changed_json << field;
  }
  changed_json << "]";

  std::ostringstream result;
  result << "{"
         << "\"kind\":\"customVarsSyncTab\","
         << "\"sourcePrefabId\":" << source_prefab_id << ","
         << "\"sourcePrefabName\":" << json::quote(prefab_name_by_id(file, source_prefab_id)) << ","
         << "\"sourceVariableCount\":" << source_entries.size() << ","
         << "\"tab\":" << json::quote(tab_label) << ","
         << "\"targetCount\":" << targets.size() << ","
         << "\"changedTopFields\":" << changed_json.str() << ","
         << "\"items\":[";
  for (size_t i = 0; i < item_jsons.size(); ++i) {
    if (i) result << ",";
    result << item_jsons[i];
  }
  result << "]}";

  CustomVarsMutation mutation;
  mutation.bytes = std::move(final_bytes);
  mutation.payload = std::vector<uint8_t>(payload(current).begin(), payload(current).end());
  mutation.result_json = result.str();
  mutation.changed_top_fields.assign(changed_fields.begin(), changed_fields.end());
  return mutation;
}

CustomVarsMutation sync_tab_custom_variables(
    const GilFile& file,
    uint64_t source_prefab_id,
    const std::string& tab_name) {
  return sync_tab_custom_variables_impl(
      file,
      source_prefab_id,
      tab_name,
      [&](const TabInfo& tab) { return tab.name == tab_name; });
}

CustomVarsMutation sync_tab_custom_variables_by_tab_id(
    const GilFile& file,
    uint64_t source_prefab_id,
    uint64_t tab_id) {
  return sync_tab_custom_variables_impl(
      file,
      source_prefab_id,
      std::to_string(tab_id),
      [&](const TabInfo& tab) { return tab.id && *tab.id == tab_id; });
}

std::string custom_variables_list_to_json(const std::vector<PrefabCustomVariables>& rows) {
  std::ostringstream out;
  out << "{\"count\":" << rows.size() << ",\"items\":[";
  for (size_t i = 0; i < rows.size(); ++i) {
    if (i) out << ",";
    out << "{"
        << "\"prefabId\":" << rows[i].prefab_id << ","
        << "\"prefabName\":" << json::quote(rows[i].prefab_name) << ","
        << "\"variables\":[";
    for (size_t j = 0; j < rows[i].variables.size(); ++j) {
      if (j) out << ",";
      const auto& variable = rows[i].variables[j];
      out << "{"
          << "\"name\":" << json::quote(variable.name) << ","
          << "\"typeId\":" << variable.type_id << ","
          << "\"type\":" << json::quote(variable.type) << ","
          << "\"enabled\":" << (variable.enabled ? json::number(*variable.enabled) : "null")
          << "}";
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}

}  // namespace opengil
