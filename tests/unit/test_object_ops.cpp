#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/object_ops.hpp"
#include "opengil/wire.hpp"

namespace {

opengil::OwnedField varint_field(uint32_t number, uint64_t value) {
  opengil::OwnedField field;
  field.number = number;
  field.wire = 0;
  field.varint = value;
  return field;
}

opengil::OwnedField len_field(uint32_t number, std::vector<uint8_t> data) {
  opengil::OwnedField field;
  field.number = number;
  field.wire = 2;
  field.data = std::move(data);
  return field;
}

std::vector<uint8_t> float_bytes(float value) {
  uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof(float));
  return {
      static_cast<uint8_t>(raw & 0xff),
      static_cast<uint8_t>((raw >> 8) & 0xff),
      static_cast<uint8_t>((raw >> 16) & 0xff),
      static_cast<uint8_t>((raw >> 24) & 0xff),
  };
}

opengil::OwnedField fixed32_field(uint32_t number, float value) {
  opengil::OwnedField field;
  field.number = number;
  field.wire = 5;
  field.data = float_bytes(value);
  return field;
}

std::vector<uint8_t> message(std::initializer_list<opengil::OwnedField> fields) {
  return opengil::rebuild_message(std::vector<opengil::OwnedField>(fields));
}

std::vector<uint8_t> vec3(float x, float y, float z) {
  return message({
      fixed32_field(1, x),
      fixed32_field(2, y),
      fixed32_field(3, z),
  });
}

std::vector<uint8_t> transform(float x, float y, float z) {
  return message({
      len_field(1, vec3(x, y, z)),
      len_field(2, vec3(0.0f, 0.0f, 0.0f)),
      len_field(3, vec3(1.0f, 1.0f, 1.0f)),
      varint_field(501, 0xffffffffull),
  });
}

std::vector<uint8_t> scene_like_entry(uint64_t object_id) {
  return message({
      varint_field(1, object_id),
      len_field(6, message({
          len_field(11, transform(1.0f, 2.0f, 3.0f)),
      })),
  });
}

opengil::GilFile make_synthetic_file() {
  const auto top5 = message({len_field(1, scene_like_entry(501))});
  const auto top8 = message({len_field(1, scene_like_entry(801))});
  const auto payload = message({
      len_field(5, top5),
      len_field(8, top8),
  });

  opengil::GilHeader header;
  header.schema = 1;

  opengil::GilFile file;
  file.path = "synthetic-object-transform.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

template <typename Mutation>
opengil::GilFile load_mutation_as_file(const Mutation& mutation, const char* name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

std::vector<uint8_t> first_entry(const opengil::GilFile& file, uint32_t top_field) {
  const auto top = opengil::top_level_data(file, top_field);
  assert(top);
  const auto fields = opengil::parse_owned_fields(*top);
  assert(fields.size() == 1);
  return fields[0].data;
}

}  // namespace

int main() {
  const auto file = make_synthetic_file();
  opengil::Transform transform;
  transform.position = {10.0, 11.0, 12.0};
  transform.rotation = {20.0, 21.0, 22.0};
  transform.scale = {2.0, 3.0, 4.0};

  const auto scene = opengil::set_scene_transform(file, 501, transform);
  assert(scene.changed_top_fields.size() == 1);
  assert(scene.changed_top_fields[0] == 5);
  const auto scene_file = load_mutation_as_file(scene, "opengil-test-scene-transform.gil");
  assert(opengil::validate_gil(scene_file).ok);
  const auto scene_entry = first_entry(scene_file, 5);
  const std::array<uint32_t, 4> scene_pos_x{6, 11, 1, 1};
  const std::array<uint32_t, 4> scene_rot_z{6, 11, 2, 3};
  const std::array<uint32_t, 4> scene_scale_y{6, 11, 3, 2};
  assert(opengil::read_fixed32_at_path(scene_entry, scene_pos_x) == 10.0f);
  assert(opengil::read_fixed32_at_path(scene_entry, scene_rot_z) == 22.0f);
  assert(opengil::read_fixed32_at_path(scene_entry, scene_scale_y) == 3.0f);

  const auto preview = opengil::set_preview_transform(file, 801, transform);
  assert(preview.changed_top_fields.size() == 1);
  assert(preview.changed_top_fields[0] == 8);
  const auto preview_file = load_mutation_as_file(preview, "opengil-test-preview-transform.gil");
  assert(opengil::validate_gil(preview_file).ok);
  const auto preview_entry = first_entry(preview_file, 8);
  assert(opengil::read_fixed32_at_path(preview_entry, scene_pos_x) == 10.0f);
  assert(opengil::read_fixed32_at_path(preview_entry, scene_rot_z) == 22.0f);
  assert(opengil::read_fixed32_at_path(preview_entry, scene_scale_y) == 3.0f);

  return 0;
}
