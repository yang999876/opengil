#include "test_support.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"
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

std::vector<uint8_t> string_bytes(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> message(std::initializer_list<opengil::OwnedField> fields) {
  return opengil::rebuild_message(std::vector<opengil::OwnedField>(fields));
}

std::vector<uint8_t> vec2(float x, float y) {
  return message({
      fixed32_field(1, x),
      fixed32_field(2, y),
  });
}

std::vector<uint8_t> ui_position_size(float x, float y) {
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

std::vector<uint8_t> transform_snapshot() {
  return message({
      len_field(501, vec3(1.0f, 2.0f, 3.0f)),
      len_field(504, ui_position_size(10.0f, 20.0f)),
      len_field(505, ui_position_size(30.0f, 40.0f)),
      len_field(508, vec3(0.0f, 0.0f, 45.0f)),
  });
}

std::vector<uint8_t> primitive_entry() {
  return message({
      varint_field(501, 7001),
      varint_field(504, opengil::kDefaultUiPrimitiveControllerEntryId),
      len_field(12, message({len_field(501, string_bytes("rect"))})),
      len_field(13, message({
          len_field(12, message({
              len_field(501, message({len_field(502, transform_snapshot())})),
              varint_field(503, 9),
          })),
      })),
      len_field(31, message({
          varint_field(2, opengil::kUiPrimitiveRectangle),
          varint_field(4, 0xffff00ffull),
      })),
  });
}

opengil::GilFile make_file() {
  const auto top9 = message({
      len_field(502, primitive_entry()),
  });
  const auto payload = message({
      len_field(9, top9),
  });

  opengil::GilHeader header;
  header.schema = 1;

  opengil::GilFile file;
  file.path = "synthetic-ui-list.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

}  // namespace

int main() {
  const auto file = make_file();
  const auto list = opengil::list_ui_primitives(file);

  OPENGIL_CHECK(list.has_top9);
  OPENGIL_CHECK(!list.has_top46);
  OPENGIL_CHECK(list.primitives.size() == 1);

  const auto& primitive = list.primitives[0];
  OPENGIL_CHECK(primitive.primitive_index == 0);
  OPENGIL_CHECK(primitive.entry_id == 7001);
  OPENGIL_CHECK(primitive.controller_entry_id == opengil::kDefaultUiPrimitiveControllerEntryId);
  OPENGIL_CHECK(primitive.name == "rect");
  OPENGIL_CHECK(primitive.primitive_type_id == opengil::kUiPrimitiveRectangle);
  OPENGIL_CHECK(primitive.raw_color == 0xffff00ffull);
  OPENGIL_CHECK(primitive.color == -65281);
  OPENGIL_CHECK(primitive.layer == 9);
  OPENGIL_CHECK(primitive.transform.position.x == 10.0);
  OPENGIL_CHECK(primitive.transform.position.y == 20.0);
  OPENGIL_CHECK(primitive.transform.size.x == 30.0);
  OPENGIL_CHECK(primitive.transform.size.y == 40.0);
  OPENGIL_CHECK(primitive.transform.scale.x == 1.0);
  OPENGIL_CHECK(primitive.transform.scale.y == 2.0);
  OPENGIL_CHECK(primitive.transform.scale.z == 3.0);
  OPENGIL_CHECK(primitive.transform.rotation_z == 45.0);

  return 0;
}
