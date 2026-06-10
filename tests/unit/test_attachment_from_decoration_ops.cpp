#include "test_support.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "opengil/attachment_from_decoration_ops.hpp"
#include "opengil/gil.hpp"
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

opengil::OwnedField fixed32_field(uint32_t number, float value) {
  uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof(float));
  opengil::OwnedField field;
  field.number = number;
  field.wire = 5;
  field.data = {
      static_cast<uint8_t>(raw & 0xff),
      static_cast<uint8_t>((raw >> 8) & 0xff),
      static_cast<uint8_t>((raw >> 16) & 0xff),
      static_cast<uint8_t>((raw >> 24) & 0xff),
  };
  return field;
}

std::vector<uint8_t> bytes_from_string(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> message(std::initializer_list<opengil::OwnedField> fields) {
  return opengil::rebuild_message(std::vector<opengil::OwnedField>(fields));
}

std::string right_hand_name() {
  return "\xE5\x8F\xB3\xE6\x89\x8B";
}

std::string left_hand_name() {
  return "\xE5\xB7\xA6\xE6\x89\x8B";
}

std::string head_name() {
  return "\xE5\xA4\xB4";
}

std::string display_prefix() {
  return "\xE8\x87\xAA\xE5\xAE\x9A\xE4\xB9\x89\xE6\x8C\x82\xE6\x8E\xA5\xE7\x82\xB9";
}

std::vector<uint8_t> vector2(float x, float y) {
  return message({
      fixed32_field(1, x),
      fixed32_field(2, y),
  });
}

std::vector<uint8_t> vector3(float x, float y, float z) {
  return message({
      fixed32_field(1, x),
      fixed32_field(2, y),
      fixed32_field(3, z),
  });
}

std::vector<uint8_t> attachment21_entry(const std::string& name, const std::string& display_name, float x, float y, float rot_x, float rot_y) {
  return message({
      len_field(1, bytes_from_string(name)),
      len_field(2, vector2(x, y)),
      len_field(3, vector2(rot_x, rot_y)),
      len_field(502, bytes_from_string(name)),
      varint_field(504, 1),
      len_field(505, bytes_from_string(display_name)),
  });
}

std::vector<uint8_t> attachment21_wrapper(uint32_t wrapper_field) {
  return message({
      varint_field(1, 11),
      len_field(21, message({
          len_field(1, attachment21_entry(right_hand_name(), "Right Hand", 0.0f, 0.0f, 12.5f, -34.25f)),
          len_field(1, attachment21_entry("old", display_prefix() + "1", 0.0f, 0.0f, 0.0f, 0.0f)),
      })),
      len_field(wrapper_field, message({})),
  });
}

std::vector<uint8_t> attachment32_wrapper() {
  return message({
      varint_field(1, 21),
      varint_field(2, 1),
      len_field(32, message({})),
  });
}

std::vector<uint8_t> owner_block(uint64_t owner_id) {
  return message({
      varint_field(1, 40),
      len_field(50, message({varint_field(502, owner_id)})),
  });
}

std::vector<uint8_t> name_block(const std::string& name) {
  return message({
      varint_field(1, 1),
      len_field(11, message({len_field(1, bytes_from_string(name))})),
  });
}

std::vector<uint8_t> transform_block(float x, float y, float z, float rot_z, float scale_y) {
  return message({
      varint_field(1, 1),
      len_field(11, message({
          len_field(1, vector3(x, y, z)),
          len_field(2, vector3(0.0f, 0.0f, rot_z)),
          len_field(3, vector3(1.0f, scale_y, 1.0f)),
      })),
  });
}

std::vector<uint8_t> decoration_entry(uint64_t id, uint64_t owner_id, const std::string& name, float x, float y, float z, float rot_z, float scale_y) {
  return message({
      varint_field(1, id),
      len_field(4, name_block(name)),
      len_field(4, owner_block(owner_id)),
      len_field(5, transform_block(x, y, z, rot_z, scale_y)),
  });
}

opengil::GilFile make_file() {
  const auto top4 = message({
      len_field(1, message({
          varint_field(1, 101),
          len_field(7, attachment21_wrapper(13)),
          len_field(8, attachment32_wrapper()),
      })),
  });
  const auto top8 = message({
      len_field(1, message({
          varint_field(1, 201),
          len_field(2, message({varint_field(1, 101)})),
          len_field(6, attachment21_wrapper(13)),
          len_field(7, attachment32_wrapper()),
      })),
  });
  const auto top27 = message({
      len_field(1, decoration_entry(301, 101, "Head", 1.25f, -2.5f, 0.0f, 0.0f, 1.0f)),
      len_field(1, decoration_entry(302, 101, "LElbow", 4.0f, 5.0f, 0.0f, 30.0f, 2.0f)),
      len_field(1, decoration_entry(303, 999, "Head", 9.0f, 9.0f, 0.0f, 0.0f, 1.0f)),
  });
  const auto payload = message({
      len_field(4, top4),
      len_field(8, top8),
      len_field(27, top27),
  });

  opengil::GilHeader header;
  header.schema = 1;

  opengil::GilFile file;
  file.path = "synthetic-attachment-from-decoration.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

opengil::GilFile load_mutation_as_file(const opengil::AttachmentMutation& mutation) {
  const auto path = std::filesystem::temp_directory_path() / "opengil-test-attachment-from-decoration.gil";
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

std::vector<uint8_t> entry_by_id(const opengil::GilFile& file, uint32_t top_field, uint64_t id) {
  const auto top = opengil::top_level_data(file, top_field);
  OPENGIL_CHECK(top);
  const std::array<uint32_t, 1> id_path{1};
  for (const auto& field : opengil::len_fields(*top, 1)) {
    const auto entry = opengil::field_data(*top, field);
    if (opengil::read_varint_at_path(entry, id_path) == id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  OPENGIL_CHECK(false);
  return {};
}

std::vector<uint8_t> find_attachment21(std::span<const uint8_t> entry, uint32_t wrapper_field_no, const std::string& name) {
  const std::array<uint32_t, 1> tag_path{1};
  const std::array<uint32_t, 1> name_path{1};
  for (const auto& wrapper_field : opengil::len_fields(entry, wrapper_field_no)) {
    const auto wrapper = opengil::field_data(entry, wrapper_field);
    if (opengil::read_varint_at_path(wrapper, tag_path) != 11) continue;
    const auto container_field = opengil::first_len_field(wrapper, 21);
    OPENGIL_CHECK(container_field);
    const auto container = opengil::field_data(wrapper, *container_field);
    for (const auto& item_field : opengil::len_fields(container, 1)) {
      const auto item = opengil::field_data(container, item_field);
      if (opengil::read_string_at_path(item, name_path) == name) {
        return std::vector<uint8_t>(item.begin(), item.end());
      }
    }
  }
  OPENGIL_CHECK(false);
  return {};
}

bool has_attachment32(std::span<const uint8_t> entry, uint32_t wrapper_field_no, const std::string& name) {
  const std::array<uint32_t, 1> tag_path{1};
  const std::array<uint32_t, 1> enabled_path{2};
  const std::array<uint32_t, 1> name_path{1};
  for (const auto& wrapper_field : opengil::len_fields(entry, wrapper_field_no)) {
    const auto wrapper = opengil::field_data(entry, wrapper_field);
    if (opengil::read_varint_at_path(wrapper, tag_path) != 21) continue;
    if (opengil::read_varint_at_path(wrapper, enabled_path) != 1) continue;
    const auto container_field = opengil::first_len_field(wrapper, 32);
    OPENGIL_CHECK(container_field);
    const auto container = opengil::field_data(wrapper, *container_field);
    for (const auto& item_field : opengil::len_fields(container, 501)) {
      const auto item = opengil::field_data(container, item_field);
      if (opengil::read_string_at_path(item, name_path) == name) return true;
    }
  }
  return false;
}

float read_float(std::span<const uint8_t> message, std::initializer_list<uint32_t> path_values) {
  const std::vector<uint32_t> path(path_values);
  const auto value = opengil::read_fixed32_at_path(message, path);
  OPENGIL_CHECK(value);
  return *value;
}

bool near(float actual, double expected) {
  return std::abs(static_cast<double>(actual) - expected) < 0.00001;
}

}  // namespace

int main() {
  const auto file = make_file();
  const auto original_top27 = opengil::top_level_data(file, 27);
  OPENGIL_CHECK(original_top27);

  const auto mutation = opengil::add_attachment_points_from_decorations(file, 101);
  OPENGIL_CHECK(mutation.summary.prefab_id == 101);
  OPENGIL_CHECK(!mutation.summary.object_id);
  OPENGIL_CHECK(mutation.summary.scene_instance_count == 1);
  OPENGIL_CHECK((mutation.summary.names == std::vector<std::string>{left_hand_name(), head_name()}));
  OPENGIL_CHECK((mutation.summary.changed_top_fields == std::vector<uint32_t>{4, 8}));

  const auto json = opengil::attachment_from_decoration_summary_to_json(mutation.summary);
  OPENGIL_CHECK(json.find("\"kind\":\"attachmentFromDecoration\"") != std::string::npos);

  const auto changed = load_mutation_as_file(mutation);
  OPENGIL_CHECK(opengil::validate_gil(changed).ok);
  const auto changed_top27 = opengil::top_level_data(changed, 27);
  OPENGIL_CHECK(changed_top27);
  OPENGIL_CHECK(std::vector<uint8_t>(original_top27->begin(), original_top27->end()) == std::vector<uint8_t>(changed_top27->begin(), changed_top27->end()));

  const auto prefab_entry = entry_by_id(changed, 4, 101);
  const auto scene_entry = entry_by_id(changed, 8, 201);
  const auto prefab_left = find_attachment21(prefab_entry, 7, left_hand_name());
  const auto prefab_head = find_attachment21(prefab_entry, 7, head_name());
  const auto scene_left = find_attachment21(scene_entry, 6, left_hand_name());
  const auto scene_head = find_attachment21(scene_entry, 6, head_name());

  OPENGIL_CHECK(has_attachment32(prefab_entry, 8, left_hand_name()));
  OPENGIL_CHECK(has_attachment32(prefab_entry, 8, head_name()));
  OPENGIL_CHECK(has_attachment32(scene_entry, 7, left_hand_name()));
  OPENGIL_CHECK(has_attachment32(scene_entry, 7, head_name()));

  OPENGIL_CHECK(near(read_float(prefab_left, {2, 1}), 1.0));
  OPENGIL_CHECK(near(read_float(prefab_left, {2, 2}), 13.660254));
  OPENGIL_CHECK(near(read_float(prefab_left, {3, 1}), 12.5));
  OPENGIL_CHECK(near(read_float(prefab_left, {3, 2}), 34.25));
  OPENGIL_CHECK(near(read_float(scene_left, {2, 1}), 1.0));
  OPENGIL_CHECK(near(read_float(scene_left, {2, 2}), 13.660254));

  OPENGIL_CHECK(near(read_float(prefab_head, {2, 1}), 1.25));
  OPENGIL_CHECK(near(read_float(prefab_head, {2, 2}), -2.5));
  OPENGIL_CHECK(near(read_float(scene_head, {2, 1}), 1.25));
  OPENGIL_CHECK(near(read_float(scene_head, {2, 2}), -2.5));

  const std::array<uint32_t, 1> display_path{505};
  OPENGIL_CHECK(opengil::read_string_at_path(prefab_left, display_path) == display_prefix() + "2");
  OPENGIL_CHECK(opengil::read_string_at_path(prefab_head, display_path) == display_prefix() + "3");

  return 0;
}
