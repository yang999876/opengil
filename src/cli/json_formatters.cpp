#include "json_formatters.hpp"

#include <iomanip>
#include <optional>
#include <sstream>

#include "opengil/json.hpp"

namespace opengil::cli {
namespace {

std::string uint32_array_json(const std::vector<uint32_t>& values) {
  return json::array_of_numbers(values);
}

std::string uint64_array_json(const std::vector<uint64_t>& values) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out << ",";
    out << values[i];
  }
  out << "]";
  return out.str();
}

std::string string_array_json(const std::vector<std::string>& values) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out << ",";
    out << json::quote(values[i]);
  }
  out << "]";
  return out.str();
}

template <typename T>
void append_optional_number(std::ostringstream& out, const std::optional<T>& value) {
  if (value) {
    out << *value;
  } else {
    out << "null";
  }
}

void append_optional_string(std::ostringstream& out, const std::optional<std::string>& value) {
  if (value) {
    out << json::quote(*value);
  } else {
    out << "null";
  }
}

std::string vec3_to_json(const Vec3& value) {
  std::ostringstream out;
  out << "{\"x\":" << value.x << ",\"y\":" << value.y << ",\"z\":" << value.z << "}";
  return out.str();
}

std::string transform_to_json(const Transform& transform) {
  return "{\"position\":" + vec3_to_json(transform.position) +
         ",\"rotation\":" + vec3_to_json(transform.rotation) +
         ",\"scale\":" + vec3_to_json(transform.scale) + "}";
}

std::string optional_float_json(const std::optional<float>& value) {
  if (!value) return "null";
  std::ostringstream out;
  out << std::setprecision(9) << *value;
  return out.str();
}

std::string float_json(float value) {
  std::ostringstream out;
  out << std::setprecision(9) << value;
  return out.str();
}

std::string optional_float_number_json(const std::optional<float>& value) {
  if (!value) return "null";
  return float_json(*value);
}

std::string sync_counts_to_json(const CustomVarsSyncCounts& counts) {
  std::ostringstream out;
  out << "{\"prefabCount\":" << counts.prefab_count
      << ",\"sceneCount\":" << counts.scene_count
      << ",\"previewCount\":" << counts.preview_count
      << "}";
  return out.str();
}

std::string custom_variable_to_json(const CustomVariableInfo& variable) {
  std::ostringstream out;
  out << "{\"name\":" << json::quote(variable.name)
      << ",\"typeId\":" << variable.type_id
      << ",\"type\":" << json::quote(variable.type);
  if (variable.enabled) out << ",\"enabled\":" << *variable.enabled;
  out << "}";
  return out.str();
}

std::string custom_vars_items_to_json(const std::vector<CustomVarsSummary>& items) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) out << ",";
    out << custom_vars_summary_to_json(items[i]);
  }
  out << "]";
  return out.str();
}

void append_optional_ui_vec2(std::ostringstream& out, const UiVec2& value, const char* x_name, const char* y_name) {
  out << "{" << json::quote(x_name) << ":";
  append_optional_number(out, value.x);
  out << "," << json::quote(y_name) << ":";
  append_optional_number(out, value.y);
  out << "}";
}

void append_optional_ui_vec3(std::ostringstream& out, const UiVec3& value) {
  out << "{\"x\":";
  append_optional_number(out, value.x);
  out << ",\"y\":";
  append_optional_number(out, value.y);
  out << ",\"z\":";
  append_optional_number(out, value.z);
  out << "}";
}

void append_ui_transform(std::ostringstream& out, const UiPrimitiveTransform& transform) {
  out << "{\"position\":";
  append_optional_ui_vec2(out, transform.position, "x", "y");
  out << ",\"size\":";
  append_optional_ui_vec2(out, transform.size, "w", "h");
  out << ",\"scale\":";
  append_optional_ui_vec3(out, transform.scale);
  out << ",\"rotationZ\":";
  append_optional_number(out, transform.rotation_z);
  out << "}";
}

void append_ui_primitive(std::ostringstream& out, const UiPrimitive& primitive) {
  out << "{\"primitiveIndex\":" << primitive.primitive_index
      << ",\"top9Index\":" << primitive.top9_index
      << ",\"entryId\":";
  append_optional_number(out, primitive.entry_id);
  out << ",\"controllerEntryId\":";
  append_optional_number(out, primitive.controller_entry_id);
  out << ",\"name\":";
  append_optional_string(out, primitive.name);
  out << ",\"primitiveTypeId\":";
  append_optional_number(out, primitive.primitive_type_id);
  out << ",\"color\":";
  append_optional_number(out, primitive.color);
  out << ",\"rawColor\":";
  append_optional_number(out, primitive.raw_color);
  out << ",\"layer\":";
  append_optional_number(out, primitive.layer);
  out << ",\"transform\":";
  append_ui_transform(out, primitive.transform);
  out << "}";
}

std::string clone_prefab_summary_to_json_with_kind(const ClonePrefabSummary& summary, const char* kind) {
  std::ostringstream preview_x;
  preview_x << std::fixed << std::setprecision(6) << summary.preview_x;
  std::ostringstream preview_z;
  preview_z << std::fixed << std::setprecision(6) << summary.preview_z;

  std::ostringstream out;
  out << "{\"kind\":" << json::quote(kind)
      << ",\"sourcePrefabId\":" << summary.source_prefab_id
      << ",\"sourceName\":" << json::quote(summary.source_name)
      << ",\"newPrefabId\":" << summary.new_prefab_id
      << ",\"newPrefabName\":" << json::quote(summary.new_prefab_name)
      << ",\"targetTab\":{\"id\":" << (summary.target_tab_id ? json::number(*summary.target_tab_id) : "null")
      << ",\"name\":" << json::quote(summary.target_tab_name)
      << "},\"clonedDecorationCount\":" << summary.cloned_decoration_count
      << ",\"previewPos\":{\"x\":" << preview_x.str()
      << ",\"z\":" << preview_z.str()
      << "},\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

}  // namespace

std::string tabs_to_json(const std::vector<TabInfo>& tabs) {
  std::ostringstream out;
  out << "{\"count\":" << tabs.size() << ",\"items\":[";
  for (size_t i = 0; i < tabs.size(); ++i) {
    if (i) out << ",";
    out << "{\"id\":" << (tabs[i].id ? json::number(*tabs[i].id) : "null")
        << ",\"name\":" << json::quote(tabs[i].name)
        << ",\"prefabIds\":" << uint64_array_json(tabs[i].prefab_ids)
        << "}";
  }
  out << "]}";
  return out.str();
}

std::string prefabs_to_json(const std::vector<PrefabInfo>& prefabs) {
  std::ostringstream out;
  out << "{\"count\":" << prefabs.size() << ",\"items\":[";
  for (size_t i = 0; i < prefabs.size(); ++i) {
    if (i) out << ",";
    out << "{\"prefabId\":" << prefabs[i].prefab_id
        << ",\"name\":" << json::quote(prefabs[i].name)
        << ",\"modelAssetId\":" << (prefabs[i].model_asset_id ? json::number(*prefabs[i].model_asset_id) : "null")
        << "}";
  }
  out << "]}";
  return out.str();
}

std::string prefab_tabs_to_json(const std::vector<TabInfo>& tabs) {
  return tabs_to_json(tabs);
}

std::string model_info_to_json(const ModelInfo& info) {
  std::ostringstream out;
  out << "{\"prefabId\":" << info.prefab_id
      << ",\"name\":" << json::quote(info.name)
      << ",\"prefabModelAssetId\":" << (info.prefab_model_asset_id ? json::number(*info.prefab_model_asset_id) : "null")
      << ",\"sceneModelAssetIds\":" << uint64_array_json(info.scene_model_asset_ids)
      << ",\"previewModelAssetIds\":" << uint64_array_json(info.preview_model_asset_ids)
      << "}";
  return out.str();
}

std::string scene_objects_to_json(const std::vector<SceneObjectInfo>& objects) {
  std::ostringstream out;
  out << "{\"count\":" << objects.size() << ",\"items\":[";
  for (size_t i = 0; i < objects.size(); ++i) {
    if (i) out << ",";
    const auto& object = objects[i];
    out << "{\"index\":" << object.index
        << ",\"objectId\":" << object.object_id
        << ",\"name\":" << json::quote(object.name)
        << ",\"refId\":";
    append_optional_number(out, object.ref_id);
    out << ",\"prefabName\":" << json::quote(object.prefab_name)
        << ",\"assetId\":";
    append_optional_number(out, object.asset_id);
    out << ",\"prefabModelAssetId\":";
    append_optional_number(out, object.prefab_model_asset_id);
    out << ",\"transform\":{\"position\":{\"x\":" << optional_float_number_json(object.transform.position_x)
        << ",\"y\":" << optional_float_number_json(object.transform.position_y)
        << ",\"z\":" << optional_float_number_json(object.transform.position_z)
        << "},\"rotation\":{\"x\":" << optional_float_number_json(object.transform.rotation_x)
        << ",\"y\":" << optional_float_number_json(object.transform.rotation_y)
        << ",\"z\":" << optional_float_number_json(object.transform.rotation_z)
        << "},\"scale\":{\"x\":" << optional_float_number_json(object.transform.scale_x)
        << ",\"y\":" << optional_float_number_json(object.transform.scale_y)
        << ",\"z\":" << optional_float_number_json(object.transform.scale_z)
        << "}}}";
  }
  out << "]}";
  return out.str();
}

std::string nodegraphs_to_json(const std::vector<NodeGraphInfo>& graphs) {
  std::ostringstream out;
  out << "{\"count\":" << graphs.size() << ",\"items\":[";
  for (size_t i = 0; i < graphs.size(); ++i) {
    if (i) out << ",";
    out << "{\"path\":" << json::quote(graphs[i].path)
        << ",\"role\":" << json::quote(graphs[i].role)
        << ",\"id\":" << (graphs[i].id ? json::number(*graphs[i].id) : "null")
        << ",\"name\":" << json::quote(graphs[i].name)
        << ",\"nodeCount\":" << graphs[i].node_count
        << ",\"compositePinCount\":" << graphs[i].composite_pin_count
        << ",\"commentCount\":" << graphs[i].comment_count
        << ",\"graphValueCount\":" << graphs[i].graph_value_count
        << ",\"affiliationCount\":" << graphs[i].affiliation_count
        << "}";
  }
  out << "]}";
  return out.str();
}

std::string set_model_summary_to_json(const SetModelSummary& summary) {
  std::ostringstream out;
  out << "{\"prefabId\":" << summary.prefab_id
      << ",\"prefabName\":" << json::quote(summary.prefab_name)
      << ",\"modelAssetId\":" << summary.model_asset_id
      << ",\"prefabUpdated\":" << json::bool_value(summary.prefab_updated)
      << ",\"sceneUpdated\":" << summary.scene_updated
      << ",\"previewUpdated\":" << summary.preview_updated
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string projectile_motion_summary_to_json(const ProjectileMotionSummary& summary) {
  std::ostringstream out;
  out << "{\"prefabId\":" << summary.prefab_id
      << ",\"prefabName\":" << json::quote(summary.prefab_name)
      << ",\"before\":{\"x\":" << optional_float_json(summary.before_x)
      << ",\"y\":" << optional_float_json(summary.before_y)
      << ",\"gravity\":" << optional_float_json(summary.before_gravity)
      << "},\"after\":{\"x\":" << float_json(summary.after_x)
      << ",\"y\":" << float_json(summary.after_y)
      << ",\"gravity\":" << optional_float_json(summary.after_gravity)
      << "},\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string rename_prefab_summary_to_json(const RenamePrefabSummary& summary) {
  std::ostringstream out;
  out << "{\"prefabId\":" << summary.prefab_id
      << ",\"beforeName\":" << json::quote(summary.before_name)
      << ",\"afterName\":" << json::quote(summary.after_name)
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string delete_prefab_summary_to_json(const DeletePrefabSummary& summary) {
  return "{\"kind\":\"deletePrefab\",\"prefabId\":" + json::number(summary.prefab_id) +
         ",\"removedDecorationIds\":" + uint64_array_json(summary.removed_decoration_ids) +
         ",\"changedTopFields\":" + uint32_array_json(summary.changed_top_fields) + "}";
}

std::string clone_prefab_summary_to_json(const ClonePrefabSummary& summary) {
  return clone_prefab_summary_to_json_with_kind(summary, "clonePrefab");
}

std::string copy_prefab_summary_to_json(const ClonePrefabSummary& summary) {
  return clone_prefab_summary_to_json_with_kind(summary, "copyPrefabToTab");
}

std::string object_summary_to_json(const ObjectSummary& summary) {
  std::ostringstream out;
  out << "{\"kind\":" << json::quote(summary.kind);
  if (summary.object_id) out << ",\"objectId\":" << *summary.object_id;
  if (summary.prefab_id) out << ",\"prefabId\":" << *summary.prefab_id;
  if (summary.asset_id) out << ",\"assetId\":" << *summary.asset_id;
  out << ",\"transform\":" << transform_to_json(summary.transform)
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string decoration_summary_to_json(const DecorationSummary& summary) {
  std::ostringstream out;
  out << "{\"kind\":\"decorationAdd\",\"prefabId\":" << summary.prefab_id
      << ",\"sceneInstanceCount\":" << summary.scene_instance_count
      << ",\"addedPrefabDecorations\":" << summary.prefab_decoration_ids.size()
      << ",\"addedSceneDecorations\":" << summary.scene_decoration_ids.size()
      << ",\"prefabDecorationIds\":" << uint64_array_json(summary.prefab_decoration_ids)
      << ",\"sceneDecorationIds\":" << uint64_array_json(summary.scene_decoration_ids)
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string attachment_summary_to_json(const AttachmentSummary& summary) {
  std::ostringstream out;
  out << "{\"kind\":\"attachmentAdd\",\"prefabId\":" << summary.prefab_id
      << ",\"objectId\":" << (summary.object_id ? json::number(*summary.object_id) : "null")
      << ",\"sceneInstanceCount\":" << summary.scene_instance_count
      << ",\"names\":" << string_array_json(summary.names)
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string attachment_from_decoration_summary_to_json(const AttachmentSummary& summary) {
  std::ostringstream out;
  out << "{\"kind\":\"attachmentFromDecoration\",\"prefabId\":" << summary.prefab_id
      << ",\"objectId\":" << (summary.object_id ? json::number(*summary.object_id) : "null")
      << ",\"sceneInstanceCount\":" << summary.scene_instance_count
      << ",\"names\":" << string_array_json(summary.names)
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string attach_nodegraph_summary_to_json(const AttachNodegraphSummary& summary) {
  std::ostringstream out;
  out << "{\"prefabId\":" << summary.prefab_id
      << ",\"nodegraphId\":" << summary.nodegraph_id
      << ",\"nodegraphName\":" << json::quote(summary.nodegraph_name)
      << ",\"prefabUpdated\":" << json::bool_value(summary.prefab_updated)
      << ",\"alreadyAttached\":" << json::bool_value(summary.already_attached)
      << ",\"sceneUpdated\":" << summary.scene_updated
      << ",\"previewUpdated\":" << summary.preview_updated
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string attach_all_nodegraphs_summary_to_json(const AttachAllNodegraphsSummary& summary) {
  std::ostringstream out;
  out << "{\"prefabId\":" << summary.prefab_id
      << ",\"availableCount\":" << summary.available_count
      << ",\"attachedCount\":" << summary.attached_count
      << ",\"attachedNodegraphIds\":" << uint64_array_json(summary.attached_nodegraph_ids)
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << ",\"items\":[";
  for (size_t i = 0; i < summary.items.size(); ++i) {
    if (i) out << ",";
    out << attach_nodegraph_summary_to_json(summary.items[i]);
  }
  out << "]}";
  return out.str();
}

std::string custom_variables_list_to_json(const std::vector<PrefabCustomVariables>& rows) {
  std::ostringstream out;
  out << "{\"count\":" << rows.size() << ",\"items\":[";
  for (size_t i = 0; i < rows.size(); ++i) {
    if (i) out << ",";
    out << "{\"prefabId\":" << rows[i].prefab_id
        << ",\"prefabName\":" << json::quote(rows[i].prefab_name)
        << ",\"variables\":[";
    for (size_t j = 0; j < rows[i].variables.size(); ++j) {
      if (j) out << ",";
      out << custom_variable_to_json(rows[i].variables[j]);
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}

std::string custom_vars_summary_to_json(const CustomVarsSummary& summary) {
  std::ostringstream out;
  out << "{\"kind\":" << json::quote(summary.kind);
  if (summary.prefab_id) out << ",\"prefabId\":" << *summary.prefab_id;
  if (!summary.prefab_name.empty()) out << ",\"prefabName\":" << json::quote(summary.prefab_name);
  if (summary.source_prefab_id) out << ",\"sourcePrefabId\":" << *summary.source_prefab_id;
  if (!summary.source_prefab_name.empty()) out << ",\"sourcePrefabName\":" << json::quote(summary.source_prefab_name);
  if (summary.target_prefab_id) out << ",\"targetPrefabId\":" << *summary.target_prefab_id;
  if (!summary.target_prefab_name.empty()) out << ",\"targetPrefabName\":" << json::quote(summary.target_prefab_name);
  if (summary.variable) out << ",\"variable\":" << custom_variable_to_json(*summary.variable);
  if (summary.variable_count) out << ",\"variableCount\":" << summary.variable_count;
  if (summary.source_variable_count) out << ",\"sourceVariableCount\":" << summary.source_variable_count;
  if (!summary.tab.empty()) out << ",\"tab\":" << json::quote(summary.tab);
  if (summary.target_count) out << ",\"targetCount\":" << summary.target_count;
  if (summary.kind == "customVarsAdd" || summary.kind == "customVarsRemove" ||
      summary.kind == "customVarsCopyAll") {
    out << ",\"synchronized\":" << sync_counts_to_json(summary.synchronized);
  }
  out << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields);
  if (!summary.items.empty()) out << ",\"items\":" << custom_vars_items_to_json(summary.items);
  out << "}";
  return out.str();
}

std::string ui_primitive_list_to_json(const UiPrimitiveList& list) {
  std::ostringstream out;
  out << "{\"kind\":\"uiPrimitiveList\",\"controllerEntryId\":" << list.controller_entry_id
      << ",\"hasTop9\":" << json::bool_value(list.has_top9)
      << ",\"hasTop46\":" << json::bool_value(list.has_top46)
      << ",\"primitiveCount\":" << list.primitives.size()
      << ",\"primitives\":[";
  for (size_t i = 0; i < list.primitives.size(); ++i) {
    if (i) out << ",";
    append_ui_primitive(out, list.primitives[i]);
  }
  out << "]}";
  return out.str();
}

std::string ui_primitive_patch_summary_to_json(const UiPrimitivePatchSummary& summary) {
  std::ostringstream out;
  out << "{\"kind\":" << json::quote(summary.kind)
      << ",\"primitiveIndex\":" << summary.primitive_index
      << ",\"entryId\":" << (summary.entry_id ? json::number(*summary.entry_id) : "null")
      << ",\"before\":";
  append_ui_primitive(out, summary.before);
  out << ",\"after\":";
  append_ui_primitive(out, summary.after);
  out << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string ui_structure_summary_to_json(const UiStructureSummary& summary) {
  std::ostringstream out;
  out << "{\"kind\":" << json::quote(summary.kind)
      << ",\"targetControllerEntryId\":" << summary.target_controller_entry_id
      << ",\"primitiveCount\":" << summary.primitive_count
      << ",\"entryIds\":" << uint64_array_json(summary.entry_ids)
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

std::string pixel_decoration_import_summary_to_json(const PixelDecorationImportSummary& summary) {
  std::ostringstream out;
  out << "{\"kind\":\"importPixelDecorationPrefab\""
      << ",\"prefabId\":" << summary.prefab_id
      << ",\"assetId\":" << summary.asset_id
      << ",\"sourcePixelCount\":" << summary.source_pixel_count
      << ",\"decorationCount\":" << summary.decoration_count
      << ",\"mergeSameColorRects\":" << (summary.merge_same_color_rects ? "true" : "false")
      << ",\"prefabDecorationIds\":" << uint64_array_json(summary.prefab_decoration_ids)
      << ",\"changedTopFields\":" << uint32_array_json(summary.changed_top_fields)
      << "}";
  return out.str();
}

}  // namespace opengil::cli
