#include "test_support.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
      len_field(2, message({
          varint_field(1, 10003004),
      })),
      len_field(6, message({
          len_field(11, transform(1.0f, 2.0f, 3.0f)),
      })),
      varint_field(8, 10003004),
  });
}

std::vector<uint8_t> prefab_like_entry(uint64_t prefab_id) {
  return message({
      varint_field(1, prefab_id),
      varint_field(2, 1000000),
      len_field(7, message({
          len_field(11, transform(1.0f, 2.0f, 3.0f)),
      })),
  });
}

std::vector<uint8_t> category_entry(uint64_t category_id) {
  return message({
      varint_field(1, category_id),
      len_field(3, message({})),
  });
}

opengil::GilFile make_synthetic_file() {
  const auto top4 = message({len_field(1, prefab_like_entry(401))});
  const auto top5 = message({len_field(1, scene_like_entry(501))});
  const auto top8 = message({len_field(1, scene_like_entry(801))});
  const auto top6 = message({
      len_field(1, category_entry(6)),
      len_field(1, category_entry(3)),
  });
  const auto payload = message({
      len_field(4, top4),
      len_field(5, top5),
      len_field(6, top6),
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
  OPENGIL_CHECK(top);
  const auto fields = opengil::parse_owned_fields(*top);
  OPENGIL_CHECK(fields.size() == 1);
  return fields[0].data;
}

std::vector<std::vector<uint8_t>> entries(const opengil::GilFile& file, uint32_t top_field) {
  std::vector<std::vector<uint8_t>> result;
  const auto top = opengil::top_level_data(file, top_field);
  OPENGIL_CHECK(top);
  for (const auto& field : opengil::len_fields(*top, 1)) {
    const auto data = opengil::field_data(*top, field);
    result.emplace_back(data.begin(), data.end());
  }
  return result;
}

struct Mapping {
  uint64_t type = 0;
  uint64_t target_id = 0;
};

std::vector<Mapping> category_mappings(const opengil::GilFile& file, uint64_t category_id) {
  std::vector<Mapping> result;
  const auto top6 = opengil::top_level_data(file, 6);
  OPENGIL_CHECK(top6);
  const std::array<uint32_t, 1> category_path{1};
  const std::array<uint32_t, 1> mapping_type_path{1};
  const std::array<uint32_t, 1> mapping_target_path{2};
  for (const auto& field : opengil::len_fields(*top6, 1)) {
    const auto category = opengil::field_data(*top6, field);
    if (opengil::read_varint_at_path(category, category_path) != category_id) continue;
    for (const auto& payload_field : opengil::len_fields(category, 3)) {
      const auto payload = opengil::field_data(category, payload_field);
      for (const auto& mapping_field : opengil::len_fields(payload, 5)) {
        const auto mapping = opengil::field_data(payload, mapping_field);
        result.push_back({
            opengil::read_varint_at_path(mapping, mapping_type_path).value(),
            opengil::read_varint_at_path(mapping, mapping_target_path).value(),
        });
      }
    }
  }
  return result;
}

bool has_mapping(const opengil::GilFile& file, uint64_t category_id, uint64_t mapping_type, uint64_t target_id) {
  for (const auto& mapping : category_mappings(file, category_id)) {
    if (mapping.type == mapping_type && mapping.target_id == target_id) return true;
  }
  return false;
}

template <typename Fn>
void expect_throws_containing(Fn&& fn, std::string_view expected) {
  try {
    fn();
  } catch (const std::exception& error) {
    OPENGIL_CHECK(std::string(error.what()).find(expected) != std::string::npos);
    return;
  }
  OPENGIL_CHECK(false);
}

}  // namespace

int main() {
  const auto file = make_synthetic_file();
  opengil::Transform transform;
  transform.position = {10.0, 11.0, 12.0};
  transform.rotation = {20.0, 21.0, 22.0};
  transform.scale = {2.0, 3.0, 4.0};

  opengil::CreateSceneObjectOptions duplicate_scene_options;
  duplicate_scene_options.object_id = 501;
  duplicate_scene_options.transform = transform;
  expect_throws_containing(
      [&] { opengil::create_scene_object(file, 20001220, duplicate_scene_options); },
      "object id already exists");

  opengil::CreatePrefabOptions duplicate_prefab_options;
  duplicate_prefab_options.prefab_id = 401;
  duplicate_prefab_options.transform = transform;
  expect_throws_containing(
      [&] { opengil::create_prefab(file, 20001220, duplicate_prefab_options); },
      "prefab id already exists");

  opengil::CreateScenePrefabInstanceOptions duplicate_instance_options;
  duplicate_instance_options.object_id = 801;
  duplicate_instance_options.transform = transform;
  expect_throws_containing(
      [&] { opengil::create_scene_prefab_instance(file, 401, 20001220, duplicate_instance_options); },
      "object id already exists");

  const auto scene = opengil::set_scene_transform(file, 501, transform);
  OPENGIL_CHECK(scene.changed_top_fields.size() == 1);
  OPENGIL_CHECK(scene.changed_top_fields[0] == 5);
  const auto scene_file = load_mutation_as_file(scene, "opengil-test-scene-transform.gil");
  OPENGIL_CHECK(opengil::validate_gil(scene_file).ok);
  const auto scene_entry = first_entry(scene_file, 5);
  const std::array<uint32_t, 4> scene_pos_x{6, 11, 1, 1};
  const std::array<uint32_t, 4> scene_rot_z{6, 11, 2, 3};
  const std::array<uint32_t, 4> scene_scale_y{6, 11, 3, 2};
  OPENGIL_CHECK(opengil::read_fixed32_at_path(scene_entry, scene_pos_x) == 10.0f);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(scene_entry, scene_rot_z) == 22.0f);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(scene_entry, scene_scale_y) == 3.0f);

  const auto scene_asset = opengil::set_scene_object_asset_id(file, 501, 20001221);
  OPENGIL_CHECK(scene_asset.summary.kind == "sceneObjectAsset");
  OPENGIL_CHECK(scene_asset.summary.object_id == 501);
  OPENGIL_CHECK(scene_asset.summary.asset_id == 20001221);
  OPENGIL_CHECK(scene_asset.changed_top_fields.size() == 1);
  OPENGIL_CHECK(scene_asset.changed_top_fields[0] == 5);
  const auto scene_asset_file = load_mutation_as_file(scene_asset, "opengil-test-scene-asset.gil");
  OPENGIL_CHECK(opengil::validate_gil(scene_asset_file).ok);
  const auto scene_asset_entry = first_entry(scene_asset_file, 5);
  const std::array<uint32_t, 2> ref_path{2, 1};
  const std::array<uint32_t, 1> asset_path{8};
  OPENGIL_CHECK(opengil::read_varint_at_path(scene_asset_entry, ref_path) == 20001221);
  OPENGIL_CHECK(opengil::read_varint_at_path(scene_asset_entry, asset_path) == 20001221);

  const auto preview = opengil::set_preview_transform(file, 801, transform);
  OPENGIL_CHECK(preview.changed_top_fields.size() == 1);
  OPENGIL_CHECK(preview.changed_top_fields[0] == 8);
  const auto preview_file = load_mutation_as_file(preview, "opengil-test-preview-transform.gil");
  OPENGIL_CHECK(opengil::validate_gil(preview_file).ok);
  const auto preview_entry = first_entry(preview_file, 8);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(preview_entry, scene_pos_x) == 10.0f);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(preview_entry, scene_rot_z) == 22.0f);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(preview_entry, scene_scale_y) == 3.0f);

  opengil::CreateSceneObjectOptions scene_create_options;
  scene_create_options.object_id = 9001;
  scene_create_options.transform = transform;
  const auto created_scene = opengil::create_scene_object(file, 20001220, scene_create_options);
  OPENGIL_CHECK(created_scene.changed_top_fields.size() == 2);
  OPENGIL_CHECK(created_scene.changed_top_fields[0] == 5);
  OPENGIL_CHECK(created_scene.changed_top_fields[1] == 6);
  const auto created_scene_file = load_mutation_as_file(created_scene, "opengil-test-create-scene-object.gil");
  OPENGIL_CHECK(opengil::validate_gil(created_scene_file).ok);
  const auto scene_entries = entries(created_scene_file, 5);
  OPENGIL_CHECK(scene_entries.size() == 2);
  const auto new_scene_entry = std::span<const uint8_t>(scene_entries[1].data(), scene_entries[1].size());
  const std::array<uint32_t, 1> id_path{1};
  OPENGIL_CHECK(opengil::read_varint_at_path(new_scene_entry, id_path) == 9001);
  OPENGIL_CHECK(opengil::read_varint_at_path(new_scene_entry, ref_path) == 20001220);
  OPENGIL_CHECK(opengil::read_varint_at_path(new_scene_entry, asset_path) == 20001220);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(new_scene_entry, scene_pos_x) == 10.0f);
  OPENGIL_CHECK(has_mapping(created_scene_file, 3, 200, 9001));

  opengil::CreatePrefabOptions prefab_create_options;
  prefab_create_options.prefab_id = 9002;
  prefab_create_options.transform = transform;
  const auto created_prefab = opengil::create_prefab(file, 20001220, prefab_create_options);
  OPENGIL_CHECK(created_prefab.changed_top_fields.size() == 2);
  OPENGIL_CHECK(created_prefab.changed_top_fields[0] == 4);
  OPENGIL_CHECK(created_prefab.changed_top_fields[1] == 6);
  const auto created_prefab_file = load_mutation_as_file(created_prefab, "opengil-test-create-prefab.gil");
  OPENGIL_CHECK(opengil::validate_gil(created_prefab_file).ok);
  const auto prefab_entries = entries(created_prefab_file, 4);
  OPENGIL_CHECK(prefab_entries.size() == 2);
  const auto new_prefab_entry = std::span<const uint8_t>(prefab_entries[1].data(), prefab_entries[1].size());
  const std::array<uint32_t, 1> prefab_asset_path{2};
  const std::array<uint32_t, 4> prefab_pos_x{7, 11, 1, 1};
  OPENGIL_CHECK(opengil::read_varint_at_path(new_prefab_entry, id_path) == 9002);
  OPENGIL_CHECK(opengil::read_varint_at_path(new_prefab_entry, prefab_asset_path) == 20001220);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(new_prefab_entry, prefab_pos_x) == 10.0f);
  OPENGIL_CHECK(has_mapping(created_prefab_file, 6, 100, 9002));
  OPENGIL_CHECK(has_mapping(created_prefab_file, 3, 100, 9002));

  opengil::CreateScenePrefabInstanceOptions instance_options;
  instance_options.object_id = 9003;
  instance_options.transform = transform;
  const auto created_instance = opengil::create_scene_prefab_instance(file, 9002, 20001220, instance_options);
  OPENGIL_CHECK(created_instance.changed_top_fields.size() == 2);
  OPENGIL_CHECK(created_instance.changed_top_fields[0] == 5);
  OPENGIL_CHECK(created_instance.changed_top_fields[1] == 6);
  const auto created_instance_file = load_mutation_as_file(created_instance, "opengil-test-create-scene-prefab-instance.gil");
  OPENGIL_CHECK(opengil::validate_gil(created_instance_file).ok);
  const auto instance_entries = entries(created_instance_file, 5);
  OPENGIL_CHECK(instance_entries.size() == 2);
  const auto new_instance_entry = std::span<const uint8_t>(instance_entries[1].data(), instance_entries[1].size());
  OPENGIL_CHECK(opengil::read_varint_at_path(new_instance_entry, id_path) == 9003);
  OPENGIL_CHECK(opengil::read_varint_at_path(new_instance_entry, ref_path) == 9002);
  OPENGIL_CHECK(opengil::read_varint_at_path(new_instance_entry, asset_path) == 20001220);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(new_instance_entry, scene_pos_x) == 10.0f);
  OPENGIL_CHECK(has_mapping(created_instance_file, 3, 200, 9003));

  return 0;
}
