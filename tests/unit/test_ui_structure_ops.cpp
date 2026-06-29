#include "test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/ui_structure_ops.hpp"
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

std::vector<uint8_t> message(std::initializer_list<opengil::OwnedField> fields) {
  return opengil::rebuild_message(std::vector<opengil::OwnedField>(fields));
}

std::vector<uint8_t> string_bytes(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> packed_ids(std::initializer_list<uint64_t> ids) {
  std::vector<uint8_t> out;
  for (uint64_t id : ids) {
    auto encoded = opengil::encode_varint(id);
    out.insert(out.end(), encoded.begin(), encoded.end());
  }
  return out;
}

std::vector<uint8_t> vec2(float x, float y) {
  return message({
      fixed32_field(501, x),
      fixed32_field(502, y),
  });
}

std::vector<uint8_t> vec3(float x, float y, float z) {
  return message({
      fixed32_field(1, x),
      fixed32_field(2, y),
      fixed32_field(3, z),
  });
}

std::vector<uint8_t> transform_snapshot(float x, float y, float w, float h, float scale, float rotation_z) {
  return message({
      len_field(501, vec3(scale, scale + 1.0f, scale + 2.0f)),
      len_field(504, vec2(x, y)),
      len_field(505, vec2(w, h)),
      len_field(508, vec3(0.0f, 0.0f, rotation_z)),
  });
}

std::vector<uint8_t> primitive_entry(
    uint64_t entry_id,
    uint64_t controller_id,
    const std::string& name,
    uint64_t primitive_type,
    float x,
    float y) {
  return message({
      varint_field(501, entry_id),
      varint_field(504, controller_id),
      len_field(12, message({len_field(501, string_bytes(name))})),
      len_field(13, message({
          len_field(12, message({
              len_field(501, message({len_field(502, transform_snapshot(x, y, 30.0f + x, 40.0f + y, 1.0f + x, 45.0f + y))})),
              varint_field(503, 9),
          })),
      })),
      len_field(31, message({
          varint_field(2, primitive_type),
          varint_field(4, 0xffff00ffull),
      })),
  });
}

std::vector<uint8_t> controller_entry(uint64_t controller_id, std::initializer_list<uint64_t> child_ids) {
  return message({
      varint_field(501, controller_id),
      len_field(503, packed_ids(child_ids)),
      len_field(12, message({len_field(501, string_bytes("controller"))})),
  });
}

opengil::GilFile make_file(std::vector<uint8_t> top9) {
  const auto payload = message({
      len_field(1, message({varint_field(77, 88)})),
      len_field(9, std::move(top9)),
      len_field(10, message({varint_field(99, 100)})),
  });

  opengil::GilHeader header;
  header.schema = 1;
  header.head_tag = 2;
  header.file_type = 3;
  header.tail_tag = 4;

  opengil::GilFile file;
  file.path = "synthetic-ui-structure.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

opengil::GilFile target_file() {
  const auto top9 = message({
      len_field(502, controller_entry(opengil::kDefaultUiPrimitiveControllerEntryId, {7001, 7002})),
      len_field(502, primitive_entry(7001, opengil::kDefaultUiPrimitiveControllerEntryId, "a", opengil::kUiPrimitiveRectangle, 10.0f, 20.0f)),
      len_field(777, message({varint_field(1, 2)})),
      len_field(502, primitive_entry(7002, opengil::kDefaultUiPrimitiveControllerEntryId, "b", opengil::kUiPrimitiveEllipse, 11.0f, 21.0f)),
      len_field(502, message({varint_field(501, 1073741841), varint_field(7, 8)})),
  });
  return make_file(top9);
}

std::vector<uint32_t> top_fields(const opengil::GilFile& file) {
  std::vector<uint32_t> fields;
  for (const auto& field : opengil::top_level_fields(file)) fields.push_back(field.number);
  return fields;
}

opengil::GilFile mutated_file(const opengil::GilFile& base, const opengil::UiStructureMutation& mutation) {
  opengil::GilFile file;
  file.path = base.path;
  file.bytes = mutation.bytes;
  const std::span<const uint8_t> bytes(file.bytes.data(), file.bytes.size());
  file.header.left_size = opengil::read_u32_be(bytes, 0);
  file.header.schema = opengil::read_u32_be(bytes, 4);
  file.header.head_tag = opengil::read_u32_be(bytes, 8);
  file.header.file_type = opengil::read_u32_be(bytes, 12);
  file.header.proto_size = opengil::read_u32_be(bytes, 16);
  file.header.tail_tag = opengil::read_u32_be(bytes, file.bytes.size() - 4);
  return file;
}

void check_only_top9_changed(const opengil::GilFile& before, const opengil::GilFile& after) {
  OPENGIL_CHECK(top_fields(before) == top_fields(after));
  const auto before1 = *opengil::top_level_data(before, 1);
  const auto after1 = *opengil::top_level_data(after, 1);
  const auto before10 = *opengil::top_level_data(before, 10);
  const auto after10 = *opengil::top_level_data(after, 10);
  OPENGIL_CHECK(before1.size() == after1.size());
  OPENGIL_CHECK(before10.size() == after10.size());
  OPENGIL_CHECK(std::equal(before1.begin(), before1.end(), after1.begin(), after1.end()));
  OPENGIL_CHECK(std::equal(before10.begin(), before10.end(), after10.begin(), after10.end()));
}

}  // namespace

int main() {
  const auto target = target_file();

  const auto retained = opengil::retain_ui_primitives(target, {1, 0});
  const auto retained_file = mutated_file(target, retained);
  auto list = opengil::list_ui_primitives(retained_file);
  OPENGIL_CHECK(opengil::validate_gil(retained_file).ok);
  OPENGIL_CHECK(list.primitives.size() == 2);
  OPENGIL_CHECK(list.primitives[0].entry_id == 7002);
  OPENGIL_CHECK(list.primitives[1].entry_id == 7001);
  OPENGIL_CHECK(retained.summary.kind == "retainUiPrimitives");
  OPENGIL_CHECK(retained.summary.primitive_count == 2);
  OPENGIL_CHECK((retained.summary.entry_ids == std::vector<uint64_t>{7002, 7001}));
  OPENGIL_CHECK((retained.summary.changed_top_fields == std::vector<uint32_t>{9}));
  check_only_top9_changed(target, retained_file);

  return 0;
}
