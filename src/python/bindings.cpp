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

#include "opengil/gil.hpp"
#include "opengil/model_ops.hpp"
#include "opengil/object_ops.hpp"

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

template <typename T>
py::list vector_to_list(const std::vector<T>& values) {
  py::list out;
  for (const auto& value : values) out.append(value);
  return out;
}

py::object optional_u64_to_py(const std::optional<uint64_t>& value) {
  if (!value) return py::none();
  return py::int_(*value);
}

py::dict object_summary_to_dict(const opengil::ObjectSummary& summary) {
  py::dict out;
  out["kind"] = summary.kind;
  out["object_id"] = optional_u64_to_py(summary.object_id);
  out["prefab_id"] = optional_u64_to_py(summary.prefab_id);
  out["asset_id"] = optional_u64_to_py(summary.asset_id);
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

  py::dict create_scene_object(
      uint64_t asset_id,
      const py::object& object_id,
      const py::object& position,
      const py::object& rotation,
      const py::object& scale) {
    opengil::CreateSceneObjectOptions options;
    options.object_id = optional_u64_from_py(object_id);
    options.transform = transform_from_py(position, rotation, scale);
    const auto mutation = opengil::create_scene_object(file_, asset_id, options);
    apply(mutation.bytes);
    return object_summary_to_dict(mutation.summary);
  }

  py::dict set_scene_transform(
      uint64_t object_id,
      const py::object& position,
      const py::object& rotation,
      const py::object& scale) {
    const auto mutation = opengil::set_scene_transform(file_, object_id, transform_from_py(position, rotation, scale));
    apply(mutation.bytes);
    return object_summary_to_dict(mutation.summary);
  }

  py::dict set_preview_transform(
      uint64_t object_id,
      const py::object& position,
      const py::object& rotation,
      const py::object& scale) {
    const auto mutation = opengil::set_preview_transform(file_, object_id, transform_from_py(position, rotation, scale));
    apply(mutation.bytes);
    return object_summary_to_dict(mutation.summary);
  }

  py::dict set_scene_object_asset_id(uint64_t object_id, uint64_t asset_id) {
    const auto mutation = opengil::set_scene_object_asset_id(file_, object_id, asset_id);
    apply(mutation.bytes);
    return object_summary_to_dict(mutation.summary);
  }

  py::dict set_model_asset_id(uint64_t prefab_id, uint64_t asset_id) {
    const auto mutation = opengil::set_prefab_model_asset_id(file_, prefab_id, asset_id);
    apply(mutation.bytes);
    return model_summary_to_dict(mutation.model_summary);
  }

 private:
  void apply(const std::vector<uint8_t>& bytes) {
    file_ = file_from_bytes(file_.path, bytes);
  }

  opengil::GilFile file_;
};

GilDocument open_document(const std::filesystem::path& path) {
  return GilDocument::open(path);
}

}  // namespace

PYBIND11_MODULE(opengil, m) {
  m.doc() = "Python bindings for openGil .gil editing";

  py::class_<GilDocument>(m, "GilDocument")
      .def_static("open", &GilDocument::open, py::arg("path"))
      .def_property_readonly("path", &GilDocument::path)
      .def_property_readonly("sha256", &GilDocument::sha256)
      .def("validate", &GilDocument::validate)
      .def("save", &GilDocument::save, py::arg("path"))
      .def(
          "create_scene_object",
          &GilDocument::create_scene_object,
          py::arg("asset_id"),
          py::arg("object_id") = py::none(),
          py::arg("position") = py::none(),
          py::arg("rotation") = py::none(),
          py::arg("scale") = py::none())
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
      .def("set_prefab_model_asset_id", &GilDocument::set_model_asset_id, py::arg("prefab_id"), py::arg("asset_id"));

  m.def("open", &open_document, py::arg("path"));
}
