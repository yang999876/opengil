#include "test_support.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#include "opengil/attachment_ops.hpp"
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

std::vector<uint8_t> bytes_from_string(const char* text) {
  return std::vector<uint8_t>(text, text + std::char_traits<char>::length(text));
}

std::vector<uint8_t> message(std::initializer_list<opengil::OwnedField> fields) {
  return opengil::rebuild_message(std::vector<opengil::OwnedField>(fields));
}

std::vector<uint8_t> attachment21_wrapper(uint32_t wrapper_field) {
  return message({
      varint_field(1, 11),
      len_field(21, message({})),
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
  const auto payload = message({
      len_field(4, top4),
      len_field(8, top8),
  });

  opengil::GilHeader header;
  header.schema = 1;

  opengil::GilFile file;
  file.path = "synthetic-attachment.gil";
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

}  // namespace

int main() {
  const auto file = make_file();
  opengil::AttachmentPointSpec spec;
  spec.name = "left_hand";
  spec.display_name = "Left Hand";
  spec.x = 0.5;
  spec.y = 1.5;
  spec.rot_x = -37.0;
  spec.rot_y = 82.0;

  const auto mutation = opengil::add_attachment_points(file, 101, std::nullopt, {spec});
  OPENGIL_CHECK(mutation.summary.prefab_id == 101);
  OPENGIL_CHECK(!mutation.summary.object_id);
  OPENGIL_CHECK(mutation.summary.scene_instance_count == 1);
  OPENGIL_CHECK(mutation.summary.names.size() == 1);
  OPENGIL_CHECK(mutation.summary.names[0] == "left_hand");
  OPENGIL_CHECK(mutation.summary.changed_top_fields.size() == 2);

  const auto changed = load_mutation_as_file(mutation, "opengil-test-attachment-add.gil");
  OPENGIL_CHECK(opengil::validate_gil(changed).ok);

  const auto prefab_entry = entry_by_id(changed, 4, 101);
  const auto scene_entry = entry_by_id(changed, 8, 201);
  const std::array<uint32_t, 4> prefab21_name{7, 21, 1, 1};
  const std::array<uint32_t, 4> prefab32_name{8, 32, 501, 1};
  const std::array<uint32_t, 4> scene21_name{6, 21, 1, 1};
  const std::array<uint32_t, 4> scene32_name{7, 32, 501, 1};
  OPENGIL_CHECK(opengil::read_string_at_path(prefab_entry, prefab21_name) == "left_hand");
  OPENGIL_CHECK(opengil::read_string_at_path(prefab_entry, prefab32_name) == "left_hand");
  OPENGIL_CHECK(opengil::read_string_at_path(scene_entry, scene21_name) == "left_hand");
  OPENGIL_CHECK(opengil::read_string_at_path(scene_entry, scene32_name) == "left_hand");

  return 0;
}
