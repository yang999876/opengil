#include "opengil/nodegraph_ops.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
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

std::vector<uint8_t> build_nodegraph_reference_payload(uint64_t nodegraph_id) {
  std::vector<OwnedField> id_fields;
  id_fields.push_back(make_varint_field(1, 1));
  id_fields.push_back(make_varint_field(2, nodegraph_id));
  id_fields.push_back(make_varint_field(501, 20000));

  std::vector<OwnedField> inner_fields;
  inner_fields.push_back(make_len_field(1, rebuild_message(id_fields)));

  std::vector<OwnedField> outer_fields;
  outer_fields.push_back(make_len_field(1, rebuild_message(inner_fields)));
  return rebuild_message(outer_fields);
}

std::optional<uint64_t> read_reference_nodegraph_id(std::span<const uint8_t> field13_payload) {
  const std::array<uint32_t, 3> path{1, 1, 2};
  return read_varint_path(field13_payload, path);
}

bool carrier_has_nodegraph(std::span<const uint8_t> carrier, uint64_t nodegraph_id) {
  for (const auto& field : len_fields(carrier, 13)) {
    const auto data = field_data(carrier, field);
    if (read_reference_nodegraph_id(data) == nodegraph_id) return true;
  }
  return false;
}

bool carrier_head_is_nodegraph(std::span<const uint8_t> carrier) {
  const std::array<uint32_t, 1> head_path{1};
  return read_varint_path(carrier, head_path) == 3;
}

std::optional<size_t> choose_carrier_index(const std::vector<OwnedField>& fields, uint32_t carrier_field_no) {
  std::optional<size_t> first_carrier;
  std::optional<size_t> first_with_graph;

  for (size_t i = 0; i < fields.size(); ++i) {
    const auto& field = fields[i];
    if (field.number != carrier_field_no || field.wire != 2) continue;
    if (!first_carrier) first_carrier = i;
    const auto data = std::span<const uint8_t>(field.data.data(), field.data.size());
    if (carrier_head_is_nodegraph(data)) return i;
    if (!first_with_graph && !len_fields(data, 13).empty()) first_with_graph = i;
  }

  if (first_with_graph) return first_with_graph;
  return first_carrier;
}

bool append_nodegraph_to_carrier(std::vector<OwnedField>& fields, uint32_t carrier_field_no, uint64_t nodegraph_id) {
  const auto carrier_index = choose_carrier_index(fields, carrier_field_no);
  if (!carrier_index) return false;

  auto& carrier = fields[*carrier_index];
  const auto data = std::span<const uint8_t>(carrier.data.data(), carrier.data.size());
  if (carrier_has_nodegraph(data, nodegraph_id)) return false;

  auto carrier_fields = parse_owned_fields(data);
  carrier_fields.push_back(make_len_field(13, build_nodegraph_reference_payload(nodegraph_id)));
  carrier.data = rebuild_message(carrier_fields);
  return true;
}

std::optional<std::string> find_nodegraph_definition_name(const GilFile& file, uint64_t nodegraph_id) {
  for (const auto& graph : list_nodegraphs(file)) {
    if (graph.role == "definition" && graph.id == nodegraph_id) return graph.name;
  }
  return std::nullopt;
}

std::vector<uint64_t> list_definition_nodegraph_ids(const GilFile& file) {
  std::map<uint64_t, std::string> definitions;
  for (const auto& graph : list_nodegraphs(file)) {
    if (graph.role == "definition" && graph.id) definitions[*graph.id] = graph.name;
  }
  std::vector<uint64_t> ids;
  ids.reserve(definitions.size());
  for (const auto& [id, _] : definitions) ids.push_back(id);
  return ids;
}

std::vector<uint8_t> patch_prefab_top4(
    std::span<const uint8_t> top4,
    uint64_t prefab_id,
    uint64_t nodegraph_id,
    AttachNodegraphSummary& summary) {
  auto fields = parse_owned_fields(top4);
  bool found = false;

  for (auto& field : fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto entry_span = std::span<const uint8_t>(field.data.data(), field.data.size());
    const std::array<uint32_t, 1> id_path{1};
    if (read_varint_path(entry_span, id_path) != prefab_id) continue;

    auto entry_fields = parse_owned_fields(entry_span);
    const auto carrier_index = choose_carrier_index(entry_fields, 7);
    if (!carrier_index) throw std::runtime_error("target prefab does not contain field 7 carrier");
    const auto carrier_span = std::span<const uint8_t>(
        entry_fields[*carrier_index].data.data(),
        entry_fields[*carrier_index].data.size());
    if (carrier_has_nodegraph(carrier_span, nodegraph_id)) {
      summary.already_attached = true;
      found = true;
      break;
    }
    if (!append_nodegraph_to_carrier(entry_fields, 7, nodegraph_id)) {
      throw std::runtime_error("failed to append nodegraph reference to prefab carrier");
    }
    field.data = rebuild_message(entry_fields);
    summary.prefab_updated = true;
    found = true;
    break;
  }

  if (!found) throw std::runtime_error("prefab id not found");
  return rebuild_message(fields);
}

std::vector<uint8_t> patch_scene_like_top(
    std::span<const uint8_t> top_data,
    uint64_t prefab_id,
    uint64_t nodegraph_id,
    size_t& updated_count) {
  auto fields = parse_owned_fields(top_data);
  updated_count = 0;

  for (auto& field : fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto entry_span = std::span<const uint8_t>(field.data.data(), field.data.size());
    const std::array<uint32_t, 2> prefab_ref_path{2, 1};
    if (read_varint_path(entry_span, prefab_ref_path) != prefab_id) continue;

    auto entry_fields = parse_owned_fields(entry_span);
    if (append_nodegraph_to_carrier(entry_fields, 6, nodegraph_id)) {
      field.data = rebuild_message(entry_fields);
      updated_count++;
    }
  }

  return rebuild_message(fields);
}

void merge_changed_fields(std::vector<uint32_t>& target, const std::vector<uint32_t>& fields) {
  for (uint32_t field : fields) {
    if (std::find(target.begin(), target.end(), field) == target.end()) target.push_back(field);
  }
  std::sort(target.begin(), target.end());
}

GilFile file_from_mutation(const GilFile& base, const std::vector<uint8_t>& bytes) {
  GilFile next = base;
  next.bytes = bytes;
  return next;
}

}  // namespace

NodegraphMutation attach_nodegraph_to_prefab(const GilFile& file, uint64_t prefab_id, uint64_t nodegraph_id) {
  auto name = find_nodegraph_definition_name(file, nodegraph_id);
  if (!name) throw std::runtime_error("nodegraph definition id not found");

  AttachNodegraphSummary summary;
  summary.prefab_id = prefab_id;
  summary.nodegraph_id = nodegraph_id;
  summary.nodegraph_name = *name;

  const auto top4 = top_level_data(file, 4);
  if (!top4) throw std::runtime_error("top-level field 4 not found");

  auto next_top4 = patch_prefab_top4(*top4, prefab_id, nodegraph_id, summary);
  std::vector<uint8_t> next_payload(payload(file).begin(), payload(file).end());

  if (summary.prefab_updated) {
    next_payload = replace_top_level_field_data(next_payload, 4, next_top4);
    summary.changed_top_fields.push_back(4);
  }

  if (!summary.already_attached) {
    if (const auto top5 = top_level_data(file, 5)) {
      size_t updated = 0;
      auto next_top5 = patch_scene_like_top(*top5, prefab_id, nodegraph_id, updated);
      if (updated > 0) {
        next_payload = replace_top_level_field_data(next_payload, 5, next_top5);
        summary.scene_updated = updated;
        summary.changed_top_fields.push_back(5);
      }
    }

    if (const auto top8 = top_level_data(file, 8)) {
      size_t updated = 0;
      auto next_top8 = patch_scene_like_top(*top8, prefab_id, nodegraph_id, updated);
      if (updated > 0) {
        next_payload = replace_top_level_field_data(next_payload, 8, next_top8);
        summary.preview_updated = updated;
        summary.changed_top_fields.push_back(8);
      }
    }
  }

  NodegraphMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

AttachAllNodegraphsMutation attach_all_nodegraphs_to_prefab(const GilFile& file, uint64_t prefab_id) {
  AttachAllNodegraphsSummary summary;
  summary.prefab_id = prefab_id;

  const auto ids = list_definition_nodegraph_ids(file);
  summary.available_count = ids.size();

  GilFile current = file;
  std::vector<uint8_t> final_bytes = file.bytes;
  for (uint64_t id : ids) {
    const auto mutation = attach_nodegraph_to_prefab(current, prefab_id, id);
    summary.items.push_back(mutation.summary);
    if (!mutation.summary.changed_top_fields.empty()) {
      summary.attached_nodegraph_ids.push_back(id);
      summary.attached_count++;
      merge_changed_fields(summary.changed_top_fields, mutation.summary.changed_top_fields);
      final_bytes = mutation.bytes;
      current = file_from_mutation(file, final_bytes);
    }
  }

  AttachAllNodegraphsMutation mutation;
  mutation.bytes = std::move(final_bytes);
  mutation.payload = std::vector<uint8_t>(payload(current).begin(), payload(current).end());
  mutation.summary = std::move(summary);
  return mutation;
}

std::string attach_nodegraph_summary_to_json(const AttachNodegraphSummary& summary) {
  std::ostringstream out;
  out << "{"
      << "\"prefabId\":" << summary.prefab_id << ","
      << "\"nodegraphId\":" << summary.nodegraph_id << ","
      << "\"nodegraphName\":" << json::quote(summary.nodegraph_name) << ","
      << "\"prefabUpdated\":" << json::bool_value(summary.prefab_updated) << ","
      << "\"alreadyAttached\":" << json::bool_value(summary.already_attached) << ","
      << "\"sceneUpdated\":" << summary.scene_updated << ","
      << "\"previewUpdated\":" << summary.preview_updated << ","
      << "\"changedTopFields\":" << json::array_of_numbers(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string attach_all_nodegraphs_summary_to_json(const AttachAllNodegraphsSummary& summary) {
  std::ostringstream out;
  out << "{"
      << "\"prefabId\":" << summary.prefab_id << ","
      << "\"availableCount\":" << summary.available_count << ","
      << "\"attachedCount\":" << summary.attached_count << ","
      << "\"attachedNodegraphIds\":[";
  for (size_t i = 0; i < summary.attached_nodegraph_ids.size(); ++i) {
    if (i) out << ",";
    out << summary.attached_nodegraph_ids[i];
  }
  out << "],\"changedTopFields\":" << json::array_of_numbers(summary.changed_top_fields)
      << ",\"items\":[";
  for (size_t i = 0; i < summary.items.size(); ++i) {
    if (i) out << ",";
    out << attach_nodegraph_summary_to_json(summary.items[i]);
  }
  out << "]}";
  return out.str();
}

}  // namespace opengil
