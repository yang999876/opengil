#include "opengil/prefab_ops.hpp"

#include <algorithm>
#include <array>
#include <optional>
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
    auto child_fields = parse_owned_fields(field.data);
    if (replace_string_at_path(child_fields, path.subspan(1), text)) {
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

std::string find_prefab_name(const GilFile& file, uint64_t prefab_id) {
  for (const auto& prefab : list_prefabs(file)) {
    if (prefab.prefab_id == prefab_id) return prefab.name;
  }
  throw std::runtime_error("prefab id not found");
}

std::vector<uint8_t> patch_top4_name(std::span<const uint8_t> top4, uint64_t prefab_id, const std::string& new_name) {
  auto fields = parse_owned_fields(top4);
  bool found = false;

  for (auto& field : fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto entry_span = std::span<const uint8_t>(field.data.data(), field.data.size());
    const std::array<uint32_t, 1> id_path{1};
    const auto id = read_varint_path(entry_span, id_path);
    if (id != prefab_id) continue;

    auto entry_fields = parse_owned_fields(entry_span);
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

}  // namespace opengil

