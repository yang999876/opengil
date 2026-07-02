#include "opengil/semantic.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <sstream>

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

std::string read_prefab_name(std::span<const uint8_t> entry) {
  const std::array<std::array<uint32_t, 3>, 1> preferred = {{{6, 11, 1}}};
  for (const auto& path : preferred) {
    if (auto text = read_string_path(entry, path)) return normalize_visible_text(*text);
  }
  const std::array<uint32_t, 2> fallback1{6, 11};
  if (auto text = read_string_path(entry, fallback1)) return normalize_visible_text(*text);
  const std::array<uint32_t, 1> fallback2{3};
  if (auto text = read_string_path(entry, fallback2)) return normalize_visible_text(*text);
  return "";
}

std::string read_scene_like_name(std::span<const uint8_t> entry) {
  const std::array<uint32_t, 2> path{5, 11};
  if (auto text = read_string_path(entry, path)) return normalize_visible_text(*text);
  return "";
}

std::optional<uint64_t> read_entry_id(std::span<const uint8_t> entry) {
  const std::array<uint32_t, 1> path{1};
  return read_varint_path(entry, path);
}

int64_t color_from_varint(uint64_t raw) {
  return static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(raw)));
}

struct SceneObjectColorInfo {
  std::optional<int64_t> color;
  std::optional<uint64_t> raw_color;
  std::optional<uint64_t> rgb_color;
  std::optional<bool> enabled;
};

std::optional<SceneObjectColorInfo> read_scene_object_color(std::span<const uint8_t> entry) {
  const std::array<uint32_t, 1> component_id_path{1};
  const std::array<uint32_t, 1> enabled_path{1};
  const std::array<uint32_t, 1> color_path{3};
  const std::array<uint32_t, 1> rgb_path{5};

  for (const auto& component_field : len_fields(entry, 6)) {
    const auto component = field_data(entry, component_field);
    if (read_varint_path(component, component_id_path) != 22) continue;

    const auto color_payload_field = first_len_field(component, 32);
    if (!color_payload_field) continue;
    const auto color_payload = field_data(component, *color_payload_field);

    SceneObjectColorInfo info;
    if (auto enabled = read_varint_path(color_payload, enabled_path)) info.enabled = *enabled != 0;
    else info.enabled = false;
    info.raw_color = read_varint_path(color_payload, color_path);
    if (info.raw_color) info.color = color_from_varint(*info.raw_color);
    info.rgb_color = read_varint_path(color_payload, rgb_path);
    return info;
  }

  return std::nullopt;
}

template <size_t N>
std::optional<float> read_fixed32_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_fixed32_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

std::vector<Field> safe_parse(std::span<const uint8_t> message) {
  std::vector<Field> fields;
  parse_fields(message, fields);
  return fields;
}

std::set<uint64_t> collect_prefab_ids_from_tab(const GilFile& file, const std::string& tab_name) {
  std::set<uint64_t> out;
  for (const auto& tab : list_tabs(file)) {
    if (tab.name == tab_name) {
      out.insert(tab.prefab_ids.begin(), tab.prefab_ids.end());
    }
  }
  return out;
}

std::string path_to_string(const std::vector<uint32_t>& path) {
  std::ostringstream out;
  for (size_t i = 0; i < path.size(); ++i) {
    if (i) out << ".";
    out << path[i];
  }
  return out.str();
}

bool path_starts_with(const std::vector<uint32_t>& path, std::initializer_list<uint32_t> prefix) {
  if (path.size() < prefix.size()) return false;
  size_t i = 0;
  for (uint32_t value : prefix) {
    if (path[i++] != value) return false;
  }
  return true;
}

bool is_nodegraph_path(const std::vector<uint32_t>& path) {
  if (path == std::vector<uint32_t>{10, 1, 1}) return true;
  return path_starts_with(path, {4, 1, 7, 13}) ||
         path_starts_with(path, {5, 1, 6, 13}) ||
         path_starts_with(path, {8, 1, 6, 13});
}

std::optional<uint64_t> read_nodegraph_id(std::span<const uint8_t> blob) {
  const std::array<uint32_t, 2> definition_id{1, 5};
  if (auto value = read_varint_path(blob, definition_id)) return value;
  const std::array<uint32_t, 2> definition_type{1, 2};
  if (auto value = read_varint_path(blob, definition_type)) return value;
  const std::array<uint32_t, 3> reference_type{1, 1, 2};
  if (auto value = read_varint_path(blob, reference_type)) return value;
  return std::nullopt;
}

NodeGraphInfo summarize_nodegraph_blob(std::span<const uint8_t> blob, const std::vector<uint32_t>& path) {
  NodeGraphInfo info;
  info.path = path_to_string(path);
  info.role = path == std::vector<uint32_t>{10, 1, 1} ? "definition" : "reference";
  info.id = read_nodegraph_id(blob);

  const std::array<uint32_t, 1> name_path{2};
  if (auto name = read_string_path(blob, name_path)) info.name = normalize_visible_text(*name);

  const auto fields = safe_parse(blob);
  for (const auto& field : fields) {
    if (field.wire != 2) continue;
    if (field.number == 3) info.node_count++;
    else if (field.number == 4) info.composite_pin_count++;
    else if (field.number == 5) info.comment_count++;
    else if (field.number == 6) info.graph_value_count++;
    else if (field.number == 7) info.affiliation_count++;
  }
  return info;
}

void collect_nodegraphs_recursive(
    std::span<const uint8_t> message,
    std::vector<uint32_t>& path,
    std::vector<NodeGraphInfo>& out,
    size_t depth) {
  if (depth > 12) return;
  const auto fields = safe_parse(message);
  for (const auto& field : fields) {
    if (field.wire != 2) continue;
    path.push_back(field.number);
    const auto data = field_data(message, field);
    if (is_nodegraph_path(path)) {
      auto summary = summarize_nodegraph_blob(data, path);
      const bool has_content =
          summary.id.has_value() ||
          !summary.name.empty() ||
          summary.node_count > 0 ||
          summary.composite_pin_count > 0 ||
          summary.comment_count > 0 ||
          summary.graph_value_count > 0 ||
          summary.affiliation_count > 0;
      if (has_content) out.push_back(std::move(summary));
    }
    collect_nodegraphs_recursive(data, path, out, depth + 1);
    path.pop_back();
  }
}

}  // namespace

std::vector<TabInfo> list_tabs(const GilFile& file) {
  std::vector<TabInfo> tabs;
  const auto top6 = top_level_data(file, 6);
  if (!top6) return tabs;

  for (const auto& entry_field : len_fields(*top6, 1)) {
    const auto entry = field_data(*top6, entry_field);
    const std::array<uint32_t, 1> category_id_path{1};
    const auto category_id = read_varint_path(entry, category_id_path);
    if (category_id != 6) continue;

    const auto root_field = first_len_field(entry, 2);
    if (!root_field) continue;
    const auto root = field_data(entry, *root_field);
    for (const auto& child_field : len_fields(root, 4)) {
      const auto child = field_data(root, child_field);
      TabInfo tab;
      const std::array<uint32_t, 1> name_path{1};
      const std::array<uint32_t, 1> id_path{3};
      if (auto name = read_string_path(child, name_path)) tab.name = normalize_visible_text(*name);
      if (auto id = read_varint_path(child, id_path)) tab.id = id;

      for (const auto& mapping_field : len_fields(child, 5)) {
        const auto mapping = field_data(child, mapping_field);
        const std::array<uint32_t, 1> type_path{1};
        const std::array<uint32_t, 1> target_path{2};
        const auto mapping_type = read_varint_path(mapping, type_path);
        const auto target_id = read_varint_path(mapping, target_path);
        if (mapping_type == 100 && target_id) tab.prefab_ids.push_back(*target_id);
      }
      tabs.push_back(std::move(tab));
    }
  }
  return tabs;
}

std::vector<PrefabInfo> list_prefabs(const GilFile& file, const std::optional<std::string>& tab_name) {
  std::vector<PrefabInfo> prefabs;
  const auto top4 = top_level_data(file, 4);
  if (!top4) return prefabs;

  std::set<uint64_t> allowed_ids;
  if (tab_name) allowed_ids = collect_prefab_ids_from_tab(file, *tab_name);

  for (const auto& entry_field : len_fields(*top4, 1)) {
    const auto entry = field_data(*top4, entry_field);
    const auto prefab_id = read_entry_id(entry);
    if (!prefab_id) continue;
    if (tab_name && !allowed_ids.contains(*prefab_id)) continue;

    PrefabInfo info;
    info.prefab_id = *prefab_id;
    const std::array<uint32_t, 1> model_path{2};
    info.model_asset_id = read_varint_path(entry, model_path);
    info.name = read_prefab_name(entry);
    prefabs.push_back(std::move(info));
  }

  std::sort(prefabs.begin(), prefabs.end(), [](const auto& a, const auto& b) {
    return a.prefab_id < b.prefab_id;
  });
  return prefabs;
}

std::vector<TabInfo> list_prefab_tabs(const GilFile& file, uint64_t prefab_id) {
  std::vector<TabInfo> out;
  for (const auto& tab : list_tabs(file)) {
    if (std::find(tab.prefab_ids.begin(), tab.prefab_ids.end(), prefab_id) != tab.prefab_ids.end()) {
      out.push_back(tab);
    }
  }
  return out;
}

std::optional<ModelInfo> get_model_info(const GilFile& file, uint64_t prefab_id) {
  const auto prefabs = list_prefabs(file);
  const auto it = std::find_if(prefabs.begin(), prefabs.end(), [&](const PrefabInfo& info) {
    return info.prefab_id == prefab_id;
  });
  if (it == prefabs.end()) return std::nullopt;

  ModelInfo info;
  info.prefab_id = prefab_id;
  info.name = it->name;
  info.prefab_model_asset_id = it->model_asset_id;

  for (const uint32_t top_field : {5u, 8u}) {
    const auto top = top_level_data(file, top_field);
    if (!top) continue;
    for (const auto& entry_field : len_fields(*top, 1)) {
      const auto entry = field_data(*top, entry_field);
      const std::array<uint32_t, 2> prefab_ref_path{2, 1};
      const auto ref = read_varint_path(entry, prefab_ref_path);
      if (ref != prefab_id) continue;
      const std::array<uint32_t, 1> model_path{8};
      const auto model = read_varint_path(entry, model_path);
      if (!model) continue;
      if (top_field == 5) info.scene_model_asset_ids.push_back(*model);
      else info.preview_model_asset_ids.push_back(*model);
    }
  }

  return info;
}

std::vector<SceneObjectInfo> list_space_objects(const GilFile& file, uint32_t top_field_number) {
  std::vector<SceneObjectInfo> objects;
  const auto top = top_level_data(file, top_field_number);
  if (!top) return objects;

  std::map<uint64_t, PrefabInfo> prefab_by_id;
  for (const auto& prefab : list_prefabs(file)) {
    prefab_by_id[prefab.prefab_id] = prefab;
  }

  size_t index = 0;
  for (const auto& entry_field : len_fields(*top, 1)) {
    const auto entry = field_data(*top, entry_field);
    const auto object_id = read_entry_id(entry);
    if (!object_id) {
      ++index;
      continue;
    }

    SceneObjectInfo info;
    info.index = index++;
    info.object_id = *object_id;
    info.name = read_scene_like_name(entry);

    const std::array<uint32_t, 2> ref_path{2, 1};
    info.ref_id = read_varint_path(entry, ref_path);
    const std::array<uint32_t, 1> asset_path{8};
    info.asset_id = read_varint_path(entry, asset_path);

    if (auto color = read_scene_object_color(entry)) {
      info.color = color->color;
      info.raw_color = color->raw_color;
      info.rgb_color = color->rgb_color;
      info.color_enabled = color->enabled;
    }

    if (info.ref_id) {
      const auto prefab_it = prefab_by_id.find(*info.ref_id);
      if (prefab_it != prefab_by_id.end()) {
        info.prefab_name = prefab_it->second.name;
        info.prefab_model_asset_id = prefab_it->second.model_asset_id;
      }
    }

    const std::array<uint32_t, 4> pos_x{6, 11, 1, 1};
    const std::array<uint32_t, 4> pos_y{6, 11, 1, 2};
    const std::array<uint32_t, 4> pos_z{6, 11, 1, 3};
    const std::array<uint32_t, 4> rot_x{6, 11, 2, 1};
    const std::array<uint32_t, 4> rot_y{6, 11, 2, 2};
    const std::array<uint32_t, 4> rot_z{6, 11, 2, 3};
    const std::array<uint32_t, 4> scale_x{6, 11, 3, 1};
    const std::array<uint32_t, 4> scale_y{6, 11, 3, 2};
    const std::array<uint32_t, 4> scale_z{6, 11, 3, 3};
    info.transform.position_x = read_fixed32_path(entry, pos_x);
    info.transform.position_y = read_fixed32_path(entry, pos_y);
    info.transform.position_z = read_fixed32_path(entry, pos_z);
    info.transform.rotation_x = read_fixed32_path(entry, rot_x);
    info.transform.rotation_y = read_fixed32_path(entry, rot_y);
    info.transform.rotation_z = read_fixed32_path(entry, rot_z);
    info.transform.scale_x = read_fixed32_path(entry, scale_x);
    info.transform.scale_y = read_fixed32_path(entry, scale_y);
    info.transform.scale_z = read_fixed32_path(entry, scale_z);
    objects.push_back(std::move(info));
  }

  return objects;
}

std::vector<SceneObjectInfo> list_scene_objects(const GilFile& file) {
  return list_space_objects(file, 5);
}

std::vector<SceneObjectInfo> list_preview_objects(const GilFile& file) {
  return list_space_objects(file, 8);
}

std::vector<NodeGraphInfo> list_nodegraphs(const GilFile& file) {
  std::vector<NodeGraphInfo> graphs;
  std::vector<uint32_t> path;
  collect_nodegraphs_recursive(payload(file), path, graphs, 0);

  std::map<uint64_t, std::string> name_by_id;
  for (const auto& graph : graphs) {
    if (graph.id && graph.role == "definition" && !graph.name.empty()) {
      name_by_id[*graph.id] = graph.name;
    }
  }
  for (auto& graph : graphs) {
    if (graph.name.empty() && graph.id) {
      const auto it = name_by_id.find(*graph.id);
      if (it != name_by_id.end()) graph.name = it->second;
    }
  }

  std::sort(graphs.begin(), graphs.end(), [](const auto& a, const auto& b) {
    if (a.id != b.id) return a.id.value_or(0) < b.id.value_or(0);
    return a.path < b.path;
  });
  return graphs;
}

}  // namespace opengil
