#include "test_support.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/ui_patch_ops.hpp"
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

std::vector<uint8_t> ui_pair(float x, float y) {
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
      len_field(501, vec3(1.0f, 1.0f, 1.0f)),
      len_field(504, ui_pair(0.0f, 0.0f)),
      len_field(505, ui_pair(80.0f, 80.0f)),
      len_field(508, vec3(0.0f, 0.0f, 0.0f)),
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
              varint_field(503, 1),
          })),
      })),
      len_field(31, message({
          varint_field(2, opengil::kUiPrimitiveRectangle),
          varint_field(4, 0xffffffffull),
      })),
  });
}

opengil::GilFile make_file() {
  const auto top9 = message({len_field(502, primitive_entry())});
  const auto payload = message({len_field(9, top9)});

  opengil::GilHeader header;
  header.schema = 1;

  opengil::GilFile file;
  file.path = "synthetic-ui-patch.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

opengil::GilFile load_mutation_as_file(const opengil::UiPrimitivePatchMutation& mutation) {
  const auto path = std::filesystem::temp_directory_path() / "opengil-test-ui-patch.gil";
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

void assert_changed_top9_only(const opengil::UiPrimitivePatchMutation& mutation) {
  OPENGIL_CHECK((mutation.summary.changed_top_fields == std::vector<uint32_t>{9}));
}

}  // namespace

int main() {
  const auto file = make_file();

  const auto typed = opengil::set_ui_primitive_type(file, 0, opengil::kUiPrimitiveEllipse);
  assert_changed_top9_only(typed);
  auto typed_file = load_mutation_as_file(typed);
  OPENGIL_CHECK(opengil::validate_gil(typed_file).ok);
  OPENGIL_CHECK(opengil::list_ui_primitives(typed_file).primitives[0].primitive_type_id == opengil::kUiPrimitiveEllipse);

  const auto colored = opengil::set_ui_primitive_color(typed_file, 0, -65536);
  assert_changed_top9_only(colored);
  auto colored_file = load_mutation_as_file(colored);
  OPENGIL_CHECK(opengil::validate_gil(colored_file).ok);
  OPENGIL_CHECK(opengil::list_ui_primitives(colored_file).primitives[0].color == -65536);

  const auto named = opengil::set_ui_primitive_name(colored_file, 0, "renamed");
  assert_changed_top9_only(named);
  auto named_file = load_mutation_as_file(named);
  OPENGIL_CHECK(opengil::validate_gil(named_file).ok);
  OPENGIL_CHECK(opengil::list_ui_primitives(named_file).primitives[0].name == "renamed");

  const auto layered = opengil::set_ui_primitive_layer(named_file, 0, 12);
  assert_changed_top9_only(layered);
  auto layered_file = load_mutation_as_file(layered);
  OPENGIL_CHECK(opengil::validate_gil(layered_file).ok);
  OPENGIL_CHECK(opengil::list_ui_primitives(layered_file).primitives[0].layer == 12);

  opengil::UiPrimitiveTransform transform;
  transform.position.x = 10.0;
  transform.position.y = 20.0;
  transform.size.x = 30.0;
  transform.size.y = 40.0;
  transform.scale.x = 2.0;
  transform.scale.y = 3.0;
  transform.scale.z = 4.0;
  transform.rotation_z = 45.0;
  const auto transformed = opengil::set_ui_primitive_transform(layered_file, 0, transform);
  assert_changed_top9_only(transformed);
  auto transformed_file = load_mutation_as_file(transformed);
  OPENGIL_CHECK(opengil::validate_gil(transformed_file).ok);
  const auto primitive = opengil::list_ui_primitives(transformed_file).primitives[0];
  OPENGIL_CHECK(primitive.transform.position.x == 10.0);
  OPENGIL_CHECK(primitive.transform.position.y == 20.0);
  OPENGIL_CHECK(primitive.transform.size.x == 30.0);
  OPENGIL_CHECK(primitive.transform.size.y == 40.0);
  OPENGIL_CHECK(primitive.transform.scale.x == 2.0);
  OPENGIL_CHECK(primitive.transform.scale.y == 3.0);
  OPENGIL_CHECK(primitive.transform.scale.z == 4.0);
  OPENGIL_CHECK(primitive.transform.rotation_z == 45.0);

  const auto json = opengil::ui_primitive_patch_summary_to_json(transformed.summary);
  OPENGIL_CHECK(json.find("\"kind\":\"uiSetTransform\"") != std::string::npos);

  return 0;
}
