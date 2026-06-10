#include "test_support.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "opengil/custom_vars_ops.hpp"
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

std::vector<uint8_t> prefab(uint64_t id, const char* name) {
  return message({
      varint_field(1, id),
      len_field(3, bytes_from_string(name)),
  });
}

std::vector<uint8_t> scene_ref(uint64_t prefab_id) {
  return message({
      len_field(2, message({varint_field(1, prefab_id)})),
  });
}

opengil::GilFile make_synthetic_file() {
  const auto top4 = message({
      len_field(1, prefab(101, "source")),
      len_field(1, prefab(202, "target")),
  });
  const auto top5 = message({
      len_field(1, scene_ref(101)),
      len_field(1, scene_ref(202)),
  });
  const auto payload = message({
      len_field(4, top4),
      len_field(5, top5),
  });

  opengil::GilHeader header;
  header.schema = 1;

  opengil::GilFile file;
  file.path = "synthetic-custom-vars.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

opengil::GilFile load_mutation_as_file(const opengil::CustomVarsMutation& mutation, const char* name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

const opengil::PrefabCustomVariables& find_row(
    const std::vector<opengil::PrefabCustomVariables>& rows,
    uint64_t prefab_id) {
  for (const auto& row : rows) {
    if (row.prefab_id == prefab_id) return row;
  }
  OPENGIL_CHECK(false);
  return rows.front();
}

}  // namespace

int main() {
  const auto file = make_synthetic_file();
  const auto before = opengil::list_prefab_custom_variables(file);
  OPENGIL_CHECK(before.size() == 2);
  OPENGIL_CHECK(find_row(before, 101).variables.empty());
  OPENGIL_CHECK(find_row(before, 202).variables.empty());

  const auto added = opengil::add_prefab_custom_variable(file, 101, "openGilVar", "str");
  OPENGIL_CHECK(added.changed_top_fields.size() == 2);
  OPENGIL_CHECK(added.changed_top_fields[0] == 4);
  OPENGIL_CHECK(added.changed_top_fields[1] == 5);
  OPENGIL_CHECK(added.result_json.find("\"sceneCount\":1") != std::string::npos);

  const auto added_file = load_mutation_as_file(added, "opengil-test-custom-vars-add.gil");
  OPENGIL_CHECK(opengil::validate_gil(added_file).ok);
  const auto after_add = opengil::list_prefab_custom_variables(added_file);
  const auto& source_after_add = find_row(after_add, 101);
  OPENGIL_CHECK(source_after_add.variables.size() == 1);
  OPENGIL_CHECK(source_after_add.variables[0].name == "openGilVar");
  OPENGIL_CHECK(source_after_add.variables[0].type_id == 6);
  OPENGIL_CHECK(source_after_add.variables[0].type == "str");
  OPENGIL_CHECK(source_after_add.variables[0].enabled == 1);
  OPENGIL_CHECK(find_row(after_add, 202).variables.empty());

  const auto copied = opengil::copy_prefab_custom_variables(added_file, 101, 202);
  OPENGIL_CHECK(copied.result_json.find("\"variableCount\":1") != std::string::npos);
  const auto copied_file = load_mutation_as_file(copied, "opengil-test-custom-vars-copy.gil");
  OPENGIL_CHECK(opengil::validate_gil(copied_file).ok);
  const auto after_copy = opengil::list_prefab_custom_variables(copied_file);
  const auto& target_after_copy = find_row(after_copy, 202);
  OPENGIL_CHECK(target_after_copy.variables.size() == 1);
  OPENGIL_CHECK(target_after_copy.variables[0].name == "openGilVar");

  const auto removed = opengil::remove_prefab_custom_variable(copied_file, 101, "openGilVar");
  const auto removed_file = load_mutation_as_file(removed, "opengil-test-custom-vars-remove.gil");
  OPENGIL_CHECK(opengil::validate_gil(removed_file).ok);
  const auto after_remove = opengil::list_prefab_custom_variables(removed_file);
  OPENGIL_CHECK(find_row(after_remove, 101).variables.empty());
  OPENGIL_CHECK(find_row(after_remove, 202).variables.size() == 1);

  return 0;
}
