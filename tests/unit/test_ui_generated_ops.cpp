#include "test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/ui_generated_ops.hpp"
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

std::vector<uint8_t> message(std::initializer_list<opengil::OwnedField> fields) {
  return opengil::rebuild_message(std::vector<opengil::OwnedField>(fields));
}

std::vector<uint8_t> controller_entry(uint64_t controller_id, std::initializer_list<uint64_t> child_ids) {
  return message({
      varint_field(501, controller_id),
      len_field(503, packed_ids(child_ids)),
      len_field(12, message({len_field(501, string_bytes("controller"))})),
  });
}

opengil::GilFile make_file() {
  const auto top9 = message({
      len_field(502, controller_entry(opengil::kDefaultUiPrimitiveControllerEntryId, {})),
      len_field(502, message({varint_field(501, 1073741841), varint_field(7, 8)})),
  });
  const auto payload = message({
      len_field(1, message({varint_field(77, 88)})),
      len_field(9, top9),
      len_field(10, message({varint_field(99, 100)})),
  });

  opengil::GilHeader header;
  header.schema = 1;
  header.head_tag = 2;
  header.file_type = 3;
  header.tail_tag = 4;

  opengil::GilFile file;
  file.path = "synthetic-ui-generated.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
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

std::vector<uint32_t> top_fields(const opengil::GilFile& file) {
  std::vector<uint32_t> fields;
  for (const auto& field : opengil::top_level_fields(file)) fields.push_back(field.number);
  return fields;
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

bool throws_for_empty(const opengil::GilFile& file) {
  try {
    opengil::append_generated_ui_primitives(file, {});
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

bool throws_for_bad_type(const opengil::GilFile& file) {
  try {
    opengil::UiGeneratedPrimitiveSpec spec;
    spec.primitive_type_id = 1;
    opengil::append_generated_ui_primitives(file, std::span<const opengil::UiGeneratedPrimitiveSpec>(&spec, 1));
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  const auto file = make_file();

  std::vector<opengil::UiGeneratedPrimitiveSpec> specs;
  opengil::UiGeneratedPrimitiveSpec rect;
  rect.primitive_type_id = opengil::kUiPrimitiveRectangle;
  rect.x = 10.0;
  rect.y = 20.0;
  rect.width = 4.0;
  rect.height = 5.0;
  rect.color = -65536;
  rect.layer = 9;
  rect.name = "rect";
  specs.push_back(rect);

  opengil::UiGeneratedPrimitiveSpec ellipse;
  ellipse.primitive_type_id = opengil::kUiPrimitiveEllipse;
  ellipse.x = 14.0;
  ellipse.y = 25.0;
  ellipse.width = 6.0;
  ellipse.height = 7.0;
  ellipse.scale_x = 2.0;
  ellipse.scale_y = 3.0;
  ellipse.scale_z = 4.0;
  ellipse.rotation_z = 45.0;
  ellipse.color = -16711936;
  ellipse.layer = 10;
  ellipse.name = "ellipse";
  specs.push_back(ellipse);

  const auto mutation = opengil::append_generated_ui_primitives(file, specs);
  const auto changed = mutated_file(file, mutation);
  OPENGIL_CHECK(opengil::validate_gil(changed).ok);
  check_only_top9_changed(file, changed);

  const auto list = opengil::list_ui_primitives(changed);
  OPENGIL_CHECK(list.primitives.size() == 2);
  OPENGIL_CHECK(list.primitives[0].entry_id == 1073741842);
  OPENGIL_CHECK(list.primitives[0].name == "rect");
  OPENGIL_CHECK(list.primitives[0].primitive_type_id == opengil::kUiPrimitiveRectangle);
  OPENGIL_CHECK(list.primitives[0].color == -65536);
  OPENGIL_CHECK(list.primitives[0].layer == 9);
  OPENGIL_CHECK(list.primitives[0].transform.position.x == 10.0);
  OPENGIL_CHECK(list.primitives[0].transform.position.y == 20.0);
  OPENGIL_CHECK(list.primitives[0].transform.size.x == 4.0);
  OPENGIL_CHECK(list.primitives[0].transform.size.y == 5.0);
  OPENGIL_CHECK(list.primitives[0].transform.scale.x == 1.0);
  OPENGIL_CHECK(list.primitives[0].transform.scale.y == 1.0);
  OPENGIL_CHECK(list.primitives[0].transform.scale.z == 1.0);
  OPENGIL_CHECK(list.primitives[0].transform.rotation_z == 0.0);

  OPENGIL_CHECK(list.primitives[1].entry_id == 1073741843);
  OPENGIL_CHECK(list.primitives[1].name == "ellipse");
  OPENGIL_CHECK(list.primitives[1].primitive_type_id == opengil::kUiPrimitiveEllipse);
  OPENGIL_CHECK(list.primitives[1].color == -16711936);
  OPENGIL_CHECK(list.primitives[1].layer == 10);
  OPENGIL_CHECK(list.primitives[1].transform.position.x == 14.0);
  OPENGIL_CHECK(list.primitives[1].transform.position.y == 25.0);
  OPENGIL_CHECK(list.primitives[1].transform.size.x == 6.0);
  OPENGIL_CHECK(list.primitives[1].transform.size.y == 7.0);
  OPENGIL_CHECK(list.primitives[1].transform.scale.x == 2.0);
  OPENGIL_CHECK(list.primitives[1].transform.scale.y == 3.0);
  OPENGIL_CHECK(list.primitives[1].transform.scale.z == 4.0);
  OPENGIL_CHECK(list.primitives[1].transform.rotation_z == 45.0);

  OPENGIL_CHECK((mutation.summary.entry_ids == std::vector<uint64_t>{1073741842, 1073741843}));
  OPENGIL_CHECK(mutation.summary.kind == "appendGeneratedUiPrimitives");
  OPENGIL_CHECK(mutation.summary.primitive_count == 2);
  OPENGIL_CHECK((mutation.summary.changed_top_fields == std::vector<uint32_t>{9}));

  OPENGIL_CHECK(throws_for_empty(file));
  OPENGIL_CHECK(throws_for_bad_type(file));

  return 0;
}
