#include "test_support.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/projectile_ops.hpp"
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

std::vector<uint8_t> projectile_name_bytes() {
  return {
      0xe6, 0x8a, 0x9b,
      0xe7, 0x89, 0xa9,
      0xe7, 0xba, 0xbf,
      0xe6, 0x8a, 0x95,
      0xe5, 0xb0, 0x84,
  };
}

opengil::GilFile make_synthetic_file() {
  constexpr uint64_t prefab_id = 1077936385;
  const auto velocity = message({fixed32_field(1, 1.0f)});
  const auto params = message({
      len_field(1, velocity),
      fixed32_field(2, 9.8f),
  });
  const auto component_inner = message({
      len_field(502, projectile_name_bytes()),
      len_field(12, params),
  });
  const auto component = message({
      varint_field(1, 11),
      len_field(21, message({len_field(1, component_inner)})),
  });
  const auto prefab = message({
      varint_field(1, prefab_id),
      len_field(8, component),
  });
  const auto top4 = message({len_field(1, prefab)});
  const auto payload = message({len_field(4, top4)});

  opengil::GilHeader header;
  header.schema = 1;

  opengil::GilFile file;
  file.path = "synthetic-projectile.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

opengil::GilFile load_mutation_as_file(const opengil::ProjectileMutation& mutation, const char* name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

std::vector<uint8_t> first_component(const opengil::GilFile& file) {
  const auto top4 = opengil::top_level_data(file, 4);
  OPENGIL_CHECK(top4);
  const auto top_fields = opengil::parse_owned_fields(*top4);
  OPENGIL_CHECK(!top_fields.empty());
  const auto entry_fields = opengil::parse_owned_fields(top_fields[0].data);
  for (const auto& field : entry_fields) {
    if (field.number == 8 && field.wire == 2) return field.data;
  }
  OPENGIL_CHECK(false);
  return {};
}

}  // namespace

int main() {
  constexpr uint64_t prefab_id = 1077936385;
  const auto file = make_synthetic_file();

  opengil::ProjectileMotionInput input;
  input.x = 3.0f;
  input.y = 4.0f;
  input.gravity = 20.0f;

  const auto mutation = opengil::set_prefab_projectile_motion(file, prefab_id, input);
  OPENGIL_CHECK(mutation.summary.prefab_id == prefab_id);
  OPENGIL_CHECK(mutation.summary.before_x == 1.0f);
  OPENGIL_CHECK(!mutation.summary.before_y);
  OPENGIL_CHECK(mutation.summary.before_gravity == 9.8f);
  OPENGIL_CHECK(mutation.summary.after_x == 3.0f);
  OPENGIL_CHECK(mutation.summary.after_y == 4.0f);
  OPENGIL_CHECK(mutation.summary.after_gravity == 20.0f);
  OPENGIL_CHECK(mutation.summary.changed_top_fields.size() == 1);
  OPENGIL_CHECK(mutation.summary.changed_top_fields[0] == 4);

  const auto changed_file = load_mutation_as_file(mutation, "opengil-test-projectile.gil");
  const auto validation = opengil::validate_gil(changed_file);
  OPENGIL_CHECK(validation.ok);

  const auto component = first_component(changed_file);
  const std::array<uint32_t, 5> x_path{21, 1, 12, 1, 1};
  const std::array<uint32_t, 5> y_path{21, 1, 12, 1, 2};
  const std::array<uint32_t, 4> gravity_path{21, 1, 12, 2};
  OPENGIL_CHECK(opengil::read_fixed32_at_path(component, x_path) == 3.0f);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(component, y_path) == 4.0f);
  OPENGIL_CHECK(opengil::read_fixed32_at_path(component, gravity_path) == 20.0f);

  return 0;
}
