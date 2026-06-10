#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/stl.h>

#include "opengil/attachment_from_decoration_ops.hpp"
#include "opengil/attachment_ops.hpp"
#include "opengil/custom_vars_ops.hpp"
#include "opengil/decoration_ops.hpp"
#include "opengil/gil.hpp"
#include "opengil/model_ops.hpp"
#include "opengil/nodegraph_ops.hpp"
#include "opengil/object_ops.hpp"
#include "opengil/prefab_ops.hpp"
#include "opengil/projectile_ops.hpp"
#include "opengil/semantic.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/ui_patch_ops.hpp"
#include "opengil/ui_structure_ops.hpp"

namespace py = pybind11;

namespace {

opengil::GilFile file_from_bytes(const std::filesystem::path& path, std::vector<uint8_t> bytes) {
  if (bytes.size() < 24) throw std::runtime_error("file is too small to be a .gil file");
  const std::span<const uint8_t> span(bytes.data(), bytes.size());

  opengil::GilFile file;
  file.path = path;
  file.bytes = std::move(bytes);
  file.header.left_size = opengil::read_u32_be(span, 0);
  file.header.schema = opengil::read_u32_be(span, 4);
  file.header.head_tag = opengil::read_u32_be(span, 8);
  file.header.file_type = opengil::read_u32_be(span, 12);
  file.header.proto_size = opengil::read_u32_be(span, 16);
  file.header.tail_tag = opengil::read_u32_be(span, file.bytes.size() - 4);
  return file;
}

std::optional<uint64_t> optional_u64_from_py(const py::object& value) {
  if (value.is_none()) return std::nullopt;
  return value.cast<uint64_t>();
}

std::optional<std::string> optional_string_from_py(const py::object& value) {
  if (value.is_none()) return std::nullopt;
  return value.cast<std::string>();
}

std::optional<float> optional_float_from_py(const py::object& value) {
  if (value.is_none()) return std::nullopt;
  return value.cast<float>();
}

bool has_key(const py::dict& dict, const char* key) {
  return dict.contains(py::str(key));
}

py::object get_or_none(const py::dict& dict, const char* key) {
  if (!has_key(dict, key)) return py::none();
  return py::reinterpret_borrow<py::object>(dict[py::str(key)]);
}

opengil::Vec3 vec3_from_py(const py::object& value, const opengil::Vec3& defaults) {
  if (value.is_none()) return defaults;
  const auto sequence = py::reinterpret_borrow<py::sequence>(value);
  if (sequence.size() != 3) throw std::runtime_error("vec3 values must have exactly 3 numbers");
  return {
      sequence[0].cast<double>(),
      sequence[1].cast<double>(),
      sequence[2].cast<double>(),
  };
}

opengil::Transform transform_from_py(
    const py::object& position,
    const py::object& rotation,
    const py::object& scale) {
  opengil::Transform transform;
  transform.position = vec3_from_py(position, transform.position);
  transform.rotation = vec3_from_py(rotation, transform.rotation);
  transform.scale = vec3_from_py(scale, transform.scale);
  return transform;
}

opengil::Transform transform_from_dict(const py::dict& dict) {
  return transform_from_py(get_or_none(dict, "position"), get_or_none(dict, "rotation"), get_or_none(dict, "scale"));
}

opengil::UiVec2 ui_vec2_from_py(const py::object& value) {
  opengil::UiVec2 out;
  if (value.is_none()) return out;
  const auto sequence = py::reinterpret_borrow<py::sequence>(value);
  if (sequence.size() != 2) throw std::runtime_error("ui vec2 values must have exactly 2 numbers");
  out.x = sequence[0].cast<double>();
  out.y = sequence[1].cast<double>();
  return out;
}

opengil::UiVec3 ui_vec3_from_py(const py::object& value) {
  opengil::UiVec3 out;
  if (value.is_none()) return out;
  const auto sequence = py::reinterpret_borrow<py::sequence>(value);
  if (sequence.size() != 3) throw std::runtime_error("ui vec3 values must have exactly 3 numbers");
  out.x = sequence[0].cast<double>();
  out.y = sequence[1].cast<double>();
  out.z = sequence[2].cast<double>();
  return out;
}

opengil::UiPrimitiveTransform ui_transform_from_py(const py::object& value) {
  opengil::UiPrimitiveTransform out;
  if (value.is_none()) return out;
  const auto dict = value.cast<py::dict>();
  out.position = ui_vec2_from_py(get_or_none(dict, "position"));
  out.size = ui_vec2_from_py(get_or_none(dict, "size"));
  out.scale = ui_vec3_from_py(get_or_none(dict, "scale"));
  if (has_key(dict, "rotation_z")) out.rotation_z = dict[py::str("rotation_z")].cast<double>();
  return out;
}

template <typename T>
py::list vector_to_list(const std::vector<T>& values) {
  py::list out;
  for (const auto& value : values) out.append(value);
  return out;
}

template <typename T, typename Fn>
py::list vector_to_list(const std::vector<T>& values, Fn fn) {
  py::list out;
  for (const auto& value : values) out.append(fn(value));
  return out;
}

template <typename T>
py::object optional_to_py(const std::optional<T>& value) {
  if (!value) return py::none();
  return py::cast(*value);
}

py::dict vec3_to_dict(const opengil::Vec3& value) {
  py::dict out;
  out["x"] = value.x;
  out["y"] = value.y;
  out["z"] = value.z;
  return out;
}

py::dict transform_to_dict(const opengil::Transform& transform) {
  py::dict out;
  out["position"] = vec3_to_dict(transform.position);
  out["rotation"] = vec3_to_dict(transform.rotation);
  out["scale"] = vec3_to_dict(transform.scale);
  return out;
}

py::dict ui_vec2_to_dict(const opengil::UiVec2& value) {
  py::dict out;
  out["x"] = optional_to_py(value.x);
  out["y"] = optional_to_py(value.y);
  return out;
}

py::dict ui_vec3_to_dict(const opengil::UiVec3& value) {
  py::dict out;
  out["x"] = optional_to_py(value.x);
  out["y"] = optional_to_py(value.y);
  out["z"] = optional_to_py(value.z);
  return out;
}

py::dict ui_transform_to_dict(const opengil::UiPrimitiveTransform& transform) {
  py::dict out;
  out["position"] = ui_vec2_to_dict(transform.position);
  out["size"] = ui_vec2_to_dict(transform.size);
  out["scale"] = ui_vec3_to_dict(transform.scale);
  out["rotation_z"] = optional_to_py(transform.rotation_z);
  return out;
}

py::dict object_summary_to_dict(const opengil::ObjectSummary& summary) {
  py::dict out;
  out["kind"] = summary.kind;
  out["object_id"] = optional_to_py(summary.object_id);
  out["prefab_id"] = optional_to_py(summary.prefab_id);
  out["asset_id"] = optional_to_py(summary.asset_id);
  out["transform"] = transform_to_dict(summary.transform);
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict model_summary_to_dict(const opengil::SetModelSummary& summary) {
  py::dict out;
  out["prefab_id"] = summary.prefab_id;
  out["prefab_name"] = summary.prefab_name;
  out["model_asset_id"] = summary.model_asset_id;
  out["prefab_updated"] = summary.prefab_updated;
  out["scene_updated"] = summary.scene_updated;
  out["preview_updated"] = summary.preview_updated;
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict rename_summary_to_dict(const opengil::RenamePrefabSummary& summary) {
  py::dict out;
  out["prefab_id"] = summary.prefab_id;
  out["before_name"] = summary.before_name;
  out["after_name"] = summary.after_name;
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict clone_summary_to_dict(const opengil::ClonePrefabSummary& summary) {
  py::dict out;
  out["source_prefab_id"] = summary.source_prefab_id;
  out["source_name"] = summary.source_name;
  out["new_prefab_id"] = summary.new_prefab_id;
  out["new_prefab_name"] = summary.new_prefab_name;
  out["target_tab_id"] = optional_to_py(summary.target_tab_id);
  out["target_tab_name"] = summary.target_tab_name;
  out["cloned_decoration_count"] = summary.cloned_decoration_count;
  out["preview_x"] = summary.preview_x;
  out["preview_z"] = summary.preview_z;
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict delete_summary_to_dict(const opengil::DeletePrefabSummary& summary) {
  py::dict out;
  out["prefab_id"] = summary.prefab_id;
  out["removed_decoration_ids"] = vector_to_list(summary.removed_decoration_ids);
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict nodegraph_summary_to_dict(const opengil::AttachNodegraphSummary& summary) {
  py::dict out;
  out["prefab_id"] = summary.prefab_id;
  out["nodegraph_id"] = summary.nodegraph_id;
  out["nodegraph_name"] = summary.nodegraph_name;
  out["prefab_updated"] = summary.prefab_updated;
  out["already_attached"] = summary.already_attached;
  out["scene_updated"] = summary.scene_updated;
  out["preview_updated"] = summary.preview_updated;
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict all_nodegraphs_summary_to_dict(const opengil::AttachAllNodegraphsSummary& summary) {
  py::dict out;
  out["prefab_id"] = summary.prefab_id;
  out["available_count"] = summary.available_count;
  out["attached_count"] = summary.attached_count;
  out["attached_nodegraph_ids"] = vector_to_list(summary.attached_nodegraph_ids);
  out["items"] = vector_to_list(summary.items, nodegraph_summary_to_dict);
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict projectile_summary_to_dict(const opengil::ProjectileMotionSummary& summary) {
  py::dict out;
  out["prefab_id"] = summary.prefab_id;
  out["prefab_name"] = summary.prefab_name;
  out["before_x"] = optional_to_py(summary.before_x);
  out["before_y"] = optional_to_py(summary.before_y);
  out["before_gravity"] = optional_to_py(summary.before_gravity);
  out["after_x"] = summary.after_x;
  out["after_y"] = summary.after_y;
  out["after_gravity"] = optional_to_py(summary.after_gravity);
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict custom_variable_to_dict(const opengil::CustomVariableInfo& variable) {
  py::dict out;
  out["name"] = variable.name;
  out["type_id"] = variable.type_id;
  out["type"] = variable.type;
  out["enabled"] = optional_to_py(variable.enabled);
  return out;
}

py::dict prefab_custom_variables_to_dict(const opengil::PrefabCustomVariables& item) {
  py::dict out;
  out["prefab_id"] = item.prefab_id;
  out["prefab_name"] = item.prefab_name;
  out["variables"] = vector_to_list(item.variables, custom_variable_to_dict);
  return out;
}

py::dict custom_vars_summary_to_dict(const opengil::CustomVarsSummary& summary) {
  py::dict out;
  out["kind"] = summary.kind;
  out["prefab_id"] = optional_to_py(summary.prefab_id);
  out["prefab_name"] = summary.prefab_name;
  out["source_prefab_id"] = optional_to_py(summary.source_prefab_id);
  out["source_prefab_name"] = summary.source_prefab_name;
  out["target_prefab_id"] = optional_to_py(summary.target_prefab_id);
  out["target_prefab_name"] = summary.target_prefab_name;
  out["variable"] = summary.variable ? custom_variable_to_dict(*summary.variable) : py::object(py::none());
  out["variable_count"] = summary.variable_count;
  out["source_variable_count"] = summary.source_variable_count;
  out["tab"] = summary.tab;
  out["target_count"] = summary.target_count;
  py::dict sync;
  sync["prefab_count"] = summary.synchronized.prefab_count;
  sync["scene_count"] = summary.synchronized.scene_count;
  sync["preview_count"] = summary.synchronized.preview_count;
  out["synchronized"] = sync;
  out["items"] = vector_to_list(summary.items, custom_vars_summary_to_dict);
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict decoration_summary_to_dict(const opengil::DecorationSummary& summary) {
  py::dict out;
  out["prefab_id"] = summary.prefab_id;
  out["scene_instance_count"] = summary.scene_instance_count;
  out["prefab_decoration_ids"] = vector_to_list(summary.prefab_decoration_ids);
  out["scene_decoration_ids"] = vector_to_list(summary.scene_decoration_ids);
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict attachment_summary_to_dict(const opengil::AttachmentSummary& summary) {
  py::dict out;
  out["prefab_id"] = summary.prefab_id;
  out["object_id"] = optional_to_py(summary.object_id);
  out["scene_instance_count"] = summary.scene_instance_count;
  out["names"] = vector_to_list(summary.names);
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict ui_primitive_to_dict(const opengil::UiPrimitive& primitive) {
  py::dict out;
  out["primitive_index"] = primitive.primitive_index;
  out["top9_index"] = primitive.top9_index;
  out["entry_id"] = optional_to_py(primitive.entry_id);
  out["controller_entry_id"] = optional_to_py(primitive.controller_entry_id);
  out["name"] = optional_to_py(primitive.name);
  out["primitive_type_id"] = optional_to_py(primitive.primitive_type_id);
  out["color"] = optional_to_py(primitive.color);
  out["raw_color"] = optional_to_py(primitive.raw_color);
  out["layer"] = optional_to_py(primitive.layer);
  out["transform"] = ui_transform_to_dict(primitive.transform);
  return out;
}

py::dict ui_list_to_dict(const opengil::UiPrimitiveList& list) {
  py::dict out;
  out["controller_entry_id"] = list.controller_entry_id;
  out["has_top9"] = list.has_top9;
  out["has_top46"] = list.has_top46;
  out["primitives"] = vector_to_list(list.primitives, ui_primitive_to_dict);
  return out;
}

py::dict ui_patch_summary_to_dict(const opengil::UiPrimitivePatchSummary& summary) {
  py::dict out;
  out["kind"] = summary.kind;
  out["primitive_index"] = summary.primitive_index;
  out["entry_id"] = optional_to_py(summary.entry_id);
  out["before"] = ui_primitive_to_dict(summary.before);
  out["after"] = ui_primitive_to_dict(summary.after);
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict ui_structure_summary_to_dict(const opengil::UiStructureSummary& summary) {
  py::dict out;
  out["kind"] = summary.kind;
  out["target_controller_entry_id"] = summary.target_controller_entry_id;
  out["entry_ids"] = vector_to_list(summary.entry_ids);
  out["primitive_count"] = summary.primitive_count;
  out["changed_top_fields"] = vector_to_list(summary.changed_top_fields);
  return out;
}

py::dict tab_info_to_dict(const opengil::TabInfo& tab) {
  py::dict out;
  out["id"] = optional_to_py(tab.id);
  out["name"] = tab.name;
  out["prefab_ids"] = vector_to_list(tab.prefab_ids);
  return out;
}

py::dict prefab_info_to_dict(const opengil::PrefabInfo& prefab) {
  py::dict out;
  out["prefab_id"] = prefab.prefab_id;
  out["model_asset_id"] = optional_to_py(prefab.model_asset_id);
  out["name"] = prefab.name;
  return out;
}

py::dict model_info_to_dict(const opengil::ModelInfo& model) {
  py::dict out;
  out["prefab_id"] = model.prefab_id;
  out["name"] = model.name;
  out["prefab_model_asset_id"] = optional_to_py(model.prefab_model_asset_id);
  out["scene_model_asset_ids"] = vector_to_list(model.scene_model_asset_ids);
  out["preview_model_asset_ids"] = vector_to_list(model.preview_model_asset_ids);
  return out;
}

py::dict nodegraph_info_to_dict(const opengil::NodeGraphInfo& graph) {
  py::dict out;
  out["path"] = graph.path;
  out["role"] = graph.role;
  out["id"] = optional_to_py(graph.id);
  out["name"] = graph.name;
  out["node_count"] = graph.node_count;
  out["composite_pin_count"] = graph.composite_pin_count;
  out["comment_count"] = graph.comment_count;
  out["graph_value_count"] = graph.graph_value_count;
  out["affiliation_count"] = graph.affiliation_count;
  return out;
}

opengil::ClonePrefabOptions clone_options_from_py(
    const py::object& new_prefab_id,
    const py::object& prefab_id_start_after,
    double preview_x_step,
    double preview_z_step) {
  opengil::ClonePrefabOptions options;
  options.new_prefab_id = optional_u64_from_py(new_prefab_id);
  options.prefab_id_start_after = optional_u64_from_py(prefab_id_start_after);
  options.preview_x_step = preview_x_step;
  options.preview_z_step = preview_z_step;
  return options;
}

std::vector<opengil::DecorationSpec> decoration_specs_from_py(const py::object& specs) {
  std::vector<opengil::DecorationSpec> out;
  for (const auto item : specs) {
    const auto dict = py::reinterpret_borrow<py::dict>(item);
    opengil::DecorationSpec spec;
    spec.asset_id = dict[py::str("asset_id")].cast<uint64_t>();
    spec.name = has_key(dict, "name") ? dict[py::str("name")].cast<std::string>() : std::string();
    spec.transform = transform_from_dict(dict);
    out.push_back(std::move(spec));
  }
  return out;
}

std::vector<opengil::AttachmentPointSpec> attachment_specs_from_py(const py::object& specs) {
  std::vector<opengil::AttachmentPointSpec> out;
  for (const auto item : specs) {
    const auto dict = py::reinterpret_borrow<py::dict>(item);
    opengil::AttachmentPointSpec spec;
    spec.name = dict[py::str("name")].cast<std::string>();
    spec.display_name = has_key(dict, "display_name") ? dict[py::str("display_name")].cast<std::string>() : spec.name;
    if (has_key(dict, "x")) spec.x = dict[py::str("x")].cast<double>();
    if (has_key(dict, "y")) spec.y = dict[py::str("y")].cast<double>();
    if (has_key(dict, "rot_x")) spec.rot_x = dict[py::str("rot_x")].cast<double>();
    if (has_key(dict, "rot_y")) spec.rot_y = dict[py::str("rot_y")].cast<double>();
    out.push_back(std::move(spec));
  }
  return out;
}

opengil::GilFile load_template_file(const std::filesystem::path& path) {
  return opengil::load_gil_file(path);
}

class GilDocument {
 public:
  explicit GilDocument(opengil::GilFile file) : file_(std::move(file)) {}

  static GilDocument open(const std::filesystem::path& path) {
    return GilDocument(opengil::load_gil_file(path));
  }

  std::string path() const {
    return file_.path.string();
  }

  std::string sha256() const {
    return opengil::file_sha256(file_);
  }

  py::dict validate() const {
    const auto validation = opengil::validate_gil(file_);
    py::dict out;
    out["ok"] = validation.ok;
    out["errors"] = vector_to_list(validation.errors);
    out["warnings"] = vector_to_list(validation.warnings);
    return out;
  }

  void save(const std::filesystem::path& path) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("failed to open output file: " + path.string());
    stream.write(reinterpret_cast<const char*>(file_.bytes.data()), static_cast<std::streamsize>(file_.bytes.size()));
    if (!stream) throw std::runtime_error("failed to write output file: " + path.string());
  }

  py::list list_tabs() const {
    return vector_to_list(opengil::list_tabs(file_), tab_info_to_dict);
  }

  py::list list_prefabs(const py::object& tab_name) const {
    return vector_to_list(opengil::list_prefabs(file_, optional_string_from_py(tab_name)), prefab_info_to_dict);
  }

  py::list list_prefab_tabs(uint64_t prefab_id) const {
    return vector_to_list(opengil::list_prefab_tabs(file_, prefab_id), tab_info_to_dict);
  }

  py::object get_model(uint64_t prefab_id) const {
    const auto model = opengil::get_model_info(file_, prefab_id);
    if (!model) return py::none();
    return model_info_to_dict(*model);
  }

  py::list list_nodegraphs() const {
    return vector_to_list(opengil::list_nodegraphs(file_), nodegraph_info_to_dict);
  }

  py::dict list_ui_primitives(uint64_t controller_entry_id) const {
    return ui_list_to_dict(opengil::list_ui_primitives(file_, controller_entry_id));
  }

  py::list list_custom_vars(const py::object& prefab_id) const {
    return vector_to_list(
        opengil::list_prefab_custom_variables(file_, optional_u64_from_py(prefab_id)),
        prefab_custom_variables_to_dict);
  }

  py::dict create_scene_object(
      uint64_t asset_id,
      const py::object& object_id,
      const py::object& position,
      const py::object& rotation,
      const py::object& scale) {
    opengil::CreateSceneObjectOptions options;
    options.object_id = optional_u64_from_py(object_id);
    options.transform = transform_from_py(position, rotation, scale);
    return apply(opengil::create_scene_object(file_, asset_id, options), object_summary_to_dict);
  }

  py::dict create_prefab(
      uint64_t asset_id,
      const py::object& prefab_id,
      const py::object& position,
      const py::object& rotation,
      const py::object& scale,
      const py::object& template_path) {
    opengil::CreatePrefabOptions options;
    options.prefab_id = optional_u64_from_py(prefab_id);
    options.transform = transform_from_py(position, rotation, scale);
    if (template_path.is_none()) return apply(opengil::create_prefab(file_, asset_id, options), object_summary_to_dict);
    const auto template_file = load_template_file(template_path.cast<std::filesystem::path>());
    return apply(opengil::create_prefab(file_, asset_id, options, &template_file), object_summary_to_dict);
  }

  py::dict create_scene_prefab_instance(
      uint64_t prefab_id,
      uint64_t asset_id,
      const py::object& object_id,
      const py::object& position,
      const py::object& rotation,
      const py::object& scale,
      const py::object& template_path) {
    opengil::CreateScenePrefabInstanceOptions options;
    options.object_id = optional_u64_from_py(object_id);
    options.transform = transform_from_py(position, rotation, scale);
    if (template_path.is_none()) {
      return apply(opengil::create_scene_prefab_instance(file_, prefab_id, asset_id, options), object_summary_to_dict);
    }
    const auto template_file = load_template_file(template_path.cast<std::filesystem::path>());
    return apply(
        opengil::create_scene_prefab_instance(file_, prefab_id, asset_id, options, &template_file),
        object_summary_to_dict);
  }

  py::dict set_scene_transform(
      uint64_t object_id,
      const py::object& position,
      const py::object& rotation,
      const py::object& scale) {
    return apply(opengil::set_scene_transform(file_, object_id, transform_from_py(position, rotation, scale)), object_summary_to_dict);
  }

  py::dict set_preview_transform(
      uint64_t object_id,
      const py::object& position,
      const py::object& rotation,
      const py::object& scale) {
    return apply(opengil::set_preview_transform(file_, object_id, transform_from_py(position, rotation, scale)), object_summary_to_dict);
  }

  py::dict set_scene_object_asset_id(uint64_t object_id, uint64_t asset_id) {
    return apply(opengil::set_scene_object_asset_id(file_, object_id, asset_id), object_summary_to_dict);
  }

  py::dict set_model_asset_id(uint64_t prefab_id, uint64_t asset_id) {
    return apply_model(opengil::set_prefab_model_asset_id(file_, prefab_id, asset_id));
  }

  py::dict set_empty_model(uint64_t prefab_id) {
    return apply_model(opengil::set_prefab_to_empty_model(file_, prefab_id));
  }

  py::dict rename_prefab(uint64_t prefab_id, const std::string& name) {
    return apply(opengil::rename_prefab(file_, prefab_id, name), rename_summary_to_dict);
  }

  py::dict clone_prefab_into_tab(
      uint64_t source_prefab_id,
      const std::string& target_tab_name,
      const std::string& new_prefab_name,
      const py::object& new_prefab_id,
      const py::object& prefab_id_start_after,
      double preview_x_step,
      double preview_z_step) {
    const auto options = clone_options_from_py(new_prefab_id, prefab_id_start_after, preview_x_step, preview_z_step);
    return apply(
        opengil::clone_prefab_into_tab(file_, source_prefab_id, target_tab_name, new_prefab_name, options),
        clone_summary_to_dict);
  }

  py::dict clone_prefab_into_tab_by_id(
      uint64_t source_prefab_id,
      uint64_t target_tab_id,
      const std::string& new_prefab_name,
      const py::object& new_prefab_id,
      const py::object& prefab_id_start_after,
      double preview_x_step,
      double preview_z_step) {
    const auto options = clone_options_from_py(new_prefab_id, prefab_id_start_after, preview_x_step, preview_z_step);
    return apply(
        opengil::clone_prefab_into_tab_by_id(file_, source_prefab_id, target_tab_id, new_prefab_name, options),
        clone_summary_to_dict);
  }

  py::dict copy_prefab_to_tab(
      uint64_t source_prefab_id,
      const std::string& target_tab_name,
      const py::object& new_prefab_name,
      const py::object& new_prefab_id,
      const py::object& prefab_id_start_after,
      double preview_x_step,
      double preview_z_step) {
    const auto options = clone_options_from_py(new_prefab_id, prefab_id_start_after, preview_x_step, preview_z_step);
    return apply(
        opengil::copy_prefab_to_tab(file_, source_prefab_id, target_tab_name, optional_string_from_py(new_prefab_name), options),
        clone_summary_to_dict);
  }

  py::dict copy_prefab_to_tab_by_id(
      uint64_t source_prefab_id,
      uint64_t target_tab_id,
      const py::object& new_prefab_name,
      const py::object& new_prefab_id,
      const py::object& prefab_id_start_after,
      double preview_x_step,
      double preview_z_step) {
    const auto options = clone_options_from_py(new_prefab_id, prefab_id_start_after, preview_x_step, preview_z_step);
    return apply(
        opengil::copy_prefab_to_tab_by_id(file_, source_prefab_id, target_tab_id, optional_string_from_py(new_prefab_name), options),
        clone_summary_to_dict);
  }

  py::dict delete_prefab(uint64_t prefab_id) {
    return apply(opengil::delete_prefab(file_, prefab_id), delete_summary_to_dict);
  }

  py::dict attach_nodegraph(uint64_t prefab_id, uint64_t nodegraph_id) {
    return apply(opengil::attach_nodegraph_to_prefab(file_, prefab_id, nodegraph_id), nodegraph_summary_to_dict);
  }

  py::dict attach_all_nodegraphs(uint64_t prefab_id) {
    return apply(opengil::attach_all_nodegraphs_to_prefab(file_, prefab_id), all_nodegraphs_summary_to_dict);
  }

  py::dict set_projectile_motion(uint64_t prefab_id, float x, float y, const py::object& gravity) {
    opengil::ProjectileMotionInput input;
    input.x = x;
    input.y = y;
    input.gravity = optional_float_from_py(gravity);
    return apply(opengil::set_prefab_projectile_motion(file_, prefab_id, input), projectile_summary_to_dict);
  }

  py::dict set_projectile_motion_from_angle(uint64_t prefab_id, float angle_deg, float speed, const py::object& gravity) {
    return apply(
        opengil::set_prefab_projectile_motion(
            file_,
            prefab_id,
            opengil::projectile_motion_from_angle(angle_deg, speed, optional_float_from_py(gravity))),
        projectile_summary_to_dict);
  }

  py::dict add_custom_var(uint64_t prefab_id, const std::string& name, const std::string& type) {
    return apply(opengil::add_prefab_custom_variable(file_, prefab_id, name, type), custom_vars_summary_to_dict);
  }

  py::dict remove_custom_var(uint64_t prefab_id, const std::string& name) {
    return apply(opengil::remove_prefab_custom_variable(file_, prefab_id, name), custom_vars_summary_to_dict);
  }

  py::dict copy_custom_vars(uint64_t source_prefab_id, uint64_t target_prefab_id) {
    return apply(opengil::copy_prefab_custom_variables(file_, source_prefab_id, target_prefab_id), custom_vars_summary_to_dict);
  }

  py::dict sync_tab_custom_vars(uint64_t source_prefab_id, const std::string& tab_name) {
    return apply(opengil::sync_tab_custom_variables(file_, source_prefab_id, tab_name), custom_vars_summary_to_dict);
  }

  py::dict sync_tab_custom_vars_by_tab_id(uint64_t source_prefab_id, uint64_t tab_id) {
    return apply(opengil::sync_tab_custom_variables_by_tab_id(file_, source_prefab_id, tab_id), custom_vars_summary_to_dict);
  }

  py::dict add_decorations(uint64_t prefab_id, const py::object& specs) {
    return apply(opengil::add_prefab_decorations(file_, prefab_id, decoration_specs_from_py(specs)), decoration_summary_to_dict);
  }

  py::dict add_attachment_points(uint64_t prefab_id, const py::object& specs, const py::object& object_id) {
    return apply(
        opengil::add_attachment_points(file_, prefab_id, optional_u64_from_py(object_id), attachment_specs_from_py(specs)),
        attachment_summary_to_dict);
  }

  py::dict add_attachment_points_from_decorations(uint64_t prefab_id, const py::object& object_id) {
    return apply(
        opengil::add_attachment_points_from_decorations(file_, prefab_id, optional_u64_from_py(object_id)),
        attachment_summary_to_dict);
  }

  py::dict set_ui_primitive_type(size_t primitive_index, uint64_t primitive_type_id, uint64_t controller_entry_id) {
    return apply(
        opengil::set_ui_primitive_type(file_, primitive_index, primitive_type_id, controller_entry_id),
        ui_patch_summary_to_dict);
  }

  py::dict set_ui_primitive_color(size_t primitive_index, int64_t color, uint64_t controller_entry_id) {
    return apply(opengil::set_ui_primitive_color(file_, primitive_index, color, controller_entry_id), ui_patch_summary_to_dict);
  }

  py::dict set_ui_primitive_transform(size_t primitive_index, const py::object& transform, uint64_t controller_entry_id) {
    return apply(
        opengil::set_ui_primitive_transform(file_, primitive_index, ui_transform_from_py(transform), controller_entry_id),
        ui_patch_summary_to_dict);
  }

  py::dict set_ui_primitive_layer(size_t primitive_index, uint64_t layer, uint64_t controller_entry_id) {
    return apply(opengil::set_ui_primitive_layer(file_, primitive_index, layer, controller_entry_id), ui_patch_summary_to_dict);
  }

  py::dict set_ui_primitive_name(size_t primitive_index, const std::string& name, uint64_t controller_entry_id) {
    return apply(opengil::set_ui_primitive_name(file_, primitive_index, name, controller_entry_id), ui_patch_summary_to_dict);
  }

  py::dict append_ui_primitive(
      const std::filesystem::path& template_path,
      size_t template_primitive_index,
      const py::object& target_controller_entry_id,
      const py::object& entry_id) {
    const auto template_file = load_template_file(template_path);
    opengil::UiAppendOptions options;
    options.template_primitive_index = template_primitive_index;
    options.target_controller_entry_id = optional_u64_from_py(target_controller_entry_id);
    options.entry_id = optional_u64_from_py(entry_id);
    return apply(opengil::append_ui_primitive_from_template(file_, template_file, options), ui_structure_summary_to_dict);
  }

  py::dict append_many_ui_primitives(
      const std::filesystem::path& template_path,
      size_t template_primitive_index,
      const py::object& target_controller_entry_id,
      const py::object& items) {
    const auto template_file = load_template_file(template_path);
    opengil::UiAppendManyOptions options;
    options.template_primitive_index = template_primitive_index;
    options.target_controller_entry_id = optional_u64_from_py(target_controller_entry_id);
    for (const auto item : items) {
      opengil::UiAppendManyItem append_item;
      if (py::isinstance<py::dict>(item)) {
        append_item.entry_id = optional_u64_from_py(get_or_none(py::reinterpret_borrow<py::dict>(item), "entry_id"));
      } else if (!py::reinterpret_borrow<py::object>(item).is_none()) {
        append_item.entry_id = py::reinterpret_borrow<py::object>(item).cast<uint64_t>();
      }
      options.items.push_back(append_item);
    }
    return apply(opengil::append_many_ui_primitives_from_template(file_, template_file, options), ui_structure_summary_to_dict);
  }

  py::dict retain_ui_primitives(const std::vector<size_t>& primitive_indexes, const py::object& target_controller_entry_id) {
    opengil::UiRetainOptions options;
    options.target_controller_entry_id = optional_u64_from_py(target_controller_entry_id);
    return apply(opengil::retain_ui_primitives(file_, primitive_indexes, options), ui_structure_summary_to_dict);
  }

  py::dict copy_ui_primitive_transform_from_template(
      const std::filesystem::path& template_path,
      size_t primitive_index,
      size_t template_primitive_index,
      const py::object& target_controller_entry_id) {
    const auto template_file = load_template_file(template_path);
    opengil::UiCopyTransformFromTemplateOptions options;
    options.primitive_index = primitive_index;
    options.template_primitive_index = template_primitive_index;
    options.target_controller_entry_id = optional_u64_from_py(target_controller_entry_id);
    return apply(opengil::copy_ui_primitive_transform_from_template(file_, template_file, options), ui_structure_summary_to_dict);
  }

 private:
  template <typename Mutation, typename Formatter>
  py::dict apply(const Mutation& mutation, Formatter formatter) {
    file_ = file_from_bytes(file_.path, mutation.bytes);
    return formatter(mutation.summary);
  }

  py::dict apply_model(const opengil::GilMutation& mutation) {
    file_ = file_from_bytes(file_.path, mutation.bytes);
    return model_summary_to_dict(mutation.model_summary);
  }

  opengil::GilFile file_;
};

GilDocument open_document(const std::filesystem::path& path) {
  return GilDocument::open(path);
}

}  // namespace

PYBIND11_MODULE(opengil, m) {
  m.doc() = "Python bindings for openGil .gil editing";
  m.attr("UI_PRIMITIVE_RECTANGLE") = opengil::kUiPrimitiveRectangle;
  m.attr("UI_PRIMITIVE_ELLIPSE") = opengil::kUiPrimitiveEllipse;
  m.attr("UI_PRIMITIVE_TRIANGLE") = opengil::kUiPrimitiveTriangle;
  m.attr("DEFAULT_UI_PRIMITIVE_CONTROLLER_ENTRY_ID") = opengil::kDefaultUiPrimitiveControllerEntryId;

  py::class_<GilDocument>(m, "GilDocument")
      .def_static("open", &GilDocument::open, py::arg("path"))
      .def_property_readonly("path", &GilDocument::path)
      .def_property_readonly("sha256", &GilDocument::sha256)
      .def("validate", &GilDocument::validate)
      .def("save", &GilDocument::save, py::arg("path"))
      .def("list_tabs", &GilDocument::list_tabs)
      .def("list_prefabs", &GilDocument::list_prefabs, py::arg("tab_name") = py::none())
      .def("list_prefab_tabs", &GilDocument::list_prefab_tabs, py::arg("prefab_id"))
      .def("get_model", &GilDocument::get_model, py::arg("prefab_id"))
      .def("list_nodegraphs", &GilDocument::list_nodegraphs)
      .def(
          "list_ui_primitives",
          &GilDocument::list_ui_primitives,
          py::arg("controller_entry_id") = opengil::kDefaultUiPrimitiveControllerEntryId)
      .def("list_custom_vars", &GilDocument::list_custom_vars, py::arg("prefab_id") = py::none())
      .def(
          "create_scene_object",
          &GilDocument::create_scene_object,
          py::arg("asset_id"),
          py::arg("object_id") = py::none(),
          py::arg("position") = py::none(),
          py::arg("rotation") = py::none(),
          py::arg("scale") = py::none())
      .def(
          "create_prefab",
          &GilDocument::create_prefab,
          py::arg("asset_id"),
          py::arg("prefab_id") = py::none(),
          py::arg("position") = py::none(),
          py::arg("rotation") = py::none(),
          py::arg("scale") = py::none(),
          py::arg("template_path") = py::none())
      .def(
          "create_scene_prefab_instance",
          &GilDocument::create_scene_prefab_instance,
          py::arg("prefab_id"),
          py::arg("asset_id"),
          py::arg("object_id") = py::none(),
          py::arg("position") = py::none(),
          py::arg("rotation") = py::none(),
          py::arg("scale") = py::none(),
          py::arg("template_path") = py::none())
      .def(
          "set_scene_transform",
          &GilDocument::set_scene_transform,
          py::arg("object_id"),
          py::arg("position") = py::none(),
          py::arg("rotation") = py::none(),
          py::arg("scale") = py::none())
      .def(
          "set_preview_transform",
          &GilDocument::set_preview_transform,
          py::arg("object_id"),
          py::arg("position") = py::none(),
          py::arg("rotation") = py::none(),
          py::arg("scale") = py::none())
      .def(
          "set_scene_object_asset_id",
          &GilDocument::set_scene_object_asset_id,
          py::arg("object_id"),
          py::arg("asset_id"))
      .def("set_asset_id", &GilDocument::set_scene_object_asset_id, py::arg("object_id"), py::arg("asset_id"))
      .def("set_model_asset_id", &GilDocument::set_model_asset_id, py::arg("prefab_id"), py::arg("asset_id"))
      .def("set_prefab_model_asset_id", &GilDocument::set_model_asset_id, py::arg("prefab_id"), py::arg("asset_id"))
      .def("set_empty_model", &GilDocument::set_empty_model, py::arg("prefab_id"))
      .def("rename_prefab", &GilDocument::rename_prefab, py::arg("prefab_id"), py::arg("name"))
      .def(
          "clone_prefab_into_tab",
          &GilDocument::clone_prefab_into_tab,
          py::arg("source_prefab_id"),
          py::arg("target_tab_name"),
          py::arg("new_prefab_name"),
          py::arg("new_prefab_id") = py::none(),
          py::arg("prefab_id_start_after") = py::none(),
          py::arg("preview_x_step") = 1.240311,
          py::arg("preview_z_step") = 2.238042)
      .def(
          "clone_prefab_into_tab_by_id",
          &GilDocument::clone_prefab_into_tab_by_id,
          py::arg("source_prefab_id"),
          py::arg("target_tab_id"),
          py::arg("new_prefab_name"),
          py::arg("new_prefab_id") = py::none(),
          py::arg("prefab_id_start_after") = py::none(),
          py::arg("preview_x_step") = 1.240311,
          py::arg("preview_z_step") = 2.238042)
      .def(
          "copy_prefab_to_tab",
          &GilDocument::copy_prefab_to_tab,
          py::arg("source_prefab_id"),
          py::arg("target_tab_name"),
          py::arg("new_prefab_name") = py::none(),
          py::arg("new_prefab_id") = py::none(),
          py::arg("prefab_id_start_after") = py::none(),
          py::arg("preview_x_step") = 1.240311,
          py::arg("preview_z_step") = 2.238042)
      .def(
          "copy_prefab_to_tab_by_id",
          &GilDocument::copy_prefab_to_tab_by_id,
          py::arg("source_prefab_id"),
          py::arg("target_tab_id"),
          py::arg("new_prefab_name") = py::none(),
          py::arg("new_prefab_id") = py::none(),
          py::arg("prefab_id_start_after") = py::none(),
          py::arg("preview_x_step") = 1.240311,
          py::arg("preview_z_step") = 2.238042)
      .def("delete_prefab", &GilDocument::delete_prefab, py::arg("prefab_id"))
      .def("attach_nodegraph", &GilDocument::attach_nodegraph, py::arg("prefab_id"), py::arg("nodegraph_id"))
      .def("attach_all_nodegraphs", &GilDocument::attach_all_nodegraphs, py::arg("prefab_id"))
      .def(
          "set_projectile_motion",
          &GilDocument::set_projectile_motion,
          py::arg("prefab_id"),
          py::arg("x"),
          py::arg("y"),
          py::arg("gravity") = py::none())
      .def(
          "set_projectile_motion_from_angle",
          &GilDocument::set_projectile_motion_from_angle,
          py::arg("prefab_id"),
          py::arg("angle_deg"),
          py::arg("speed"),
          py::arg("gravity") = py::none())
      .def("add_custom_var", &GilDocument::add_custom_var, py::arg("prefab_id"), py::arg("name"), py::arg("type"))
      .def("remove_custom_var", &GilDocument::remove_custom_var, py::arg("prefab_id"), py::arg("name"))
      .def("copy_custom_vars", &GilDocument::copy_custom_vars, py::arg("source_prefab_id"), py::arg("target_prefab_id"))
      .def("sync_tab_custom_vars", &GilDocument::sync_tab_custom_vars, py::arg("source_prefab_id"), py::arg("tab_name"))
      .def(
          "sync_tab_custom_vars_by_tab_id",
          &GilDocument::sync_tab_custom_vars_by_tab_id,
          py::arg("source_prefab_id"),
          py::arg("tab_id"))
      .def("add_decorations", &GilDocument::add_decorations, py::arg("prefab_id"), py::arg("specs"))
      .def(
          "add_attachment_points",
          &GilDocument::add_attachment_points,
          py::arg("prefab_id"),
          py::arg("specs"),
          py::arg("object_id") = py::none())
      .def(
          "add_attachment_points_from_decorations",
          &GilDocument::add_attachment_points_from_decorations,
          py::arg("prefab_id"),
          py::arg("object_id") = py::none())
      .def(
          "set_ui_primitive_type",
          &GilDocument::set_ui_primitive_type,
          py::arg("primitive_index"),
          py::arg("primitive_type_id"),
          py::arg("controller_entry_id") = opengil::kDefaultUiPrimitiveControllerEntryId)
      .def(
          "set_ui_primitive_color",
          &GilDocument::set_ui_primitive_color,
          py::arg("primitive_index"),
          py::arg("color"),
          py::arg("controller_entry_id") = opengil::kDefaultUiPrimitiveControllerEntryId)
      .def(
          "set_ui_primitive_transform",
          &GilDocument::set_ui_primitive_transform,
          py::arg("primitive_index"),
          py::arg("transform"),
          py::arg("controller_entry_id") = opengil::kDefaultUiPrimitiveControllerEntryId)
      .def(
          "set_ui_primitive_layer",
          &GilDocument::set_ui_primitive_layer,
          py::arg("primitive_index"),
          py::arg("layer"),
          py::arg("controller_entry_id") = opengil::kDefaultUiPrimitiveControllerEntryId)
      .def(
          "set_ui_primitive_name",
          &GilDocument::set_ui_primitive_name,
          py::arg("primitive_index"),
          py::arg("name"),
          py::arg("controller_entry_id") = opengil::kDefaultUiPrimitiveControllerEntryId)
      .def(
          "append_ui_primitive",
          &GilDocument::append_ui_primitive,
          py::arg("template_path"),
          py::arg("template_primitive_index") = 0,
          py::arg("target_controller_entry_id") = py::none(),
          py::arg("entry_id") = py::none())
      .def(
          "append_many_ui_primitives",
          &GilDocument::append_many_ui_primitives,
          py::arg("template_path"),
          py::arg("template_primitive_index"),
          py::arg("target_controller_entry_id"),
          py::arg("items"))
      .def(
          "retain_ui_primitives",
          &GilDocument::retain_ui_primitives,
          py::arg("primitive_indexes"),
          py::arg("target_controller_entry_id") = py::none())
      .def(
          "copy_ui_primitive_transform_from_template",
          &GilDocument::copy_ui_primitive_transform_from_template,
          py::arg("template_path"),
          py::arg("primitive_index") = 0,
          py::arg("template_primitive_index") = 0,
          py::arg("target_controller_entry_id") = py::none());

  m.def("open", &open_document, py::arg("path"));
}
