#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "opengil/gil.hpp"
#include "opengil/prefab_ops.hpp"
#include "opengil/semantic.hpp"

namespace {

std::filesystem::path fixture_path(const char* name) {
  return std::filesystem::path(OPENGIL_TEST_FIXTURE_DIR) / name;
}

opengil::GilFile load_mutation_as_file(const opengil::PrefabRenameMutation& mutation, const char* name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

}  // namespace

int main() {
  const auto file = opengil::load_gil_file(fixture_path("test1.gil"));

  const std::string renamed = "openGil Rename Test";
  const auto mutation = opengil::rename_prefab(file, 1086324737, renamed);
  assert(mutation.summary.prefab_id == 1086324737);
  assert(mutation.summary.before_name == "Default Template");
  assert(mutation.summary.after_name == renamed);
  assert(mutation.summary.changed_top_fields.size() == 1);
  assert(mutation.summary.changed_top_fields[0] == 4);

  const auto changed_file = load_mutation_as_file(mutation, "opengil-test-rename.gil");
  const auto prefabs = opengil::list_prefabs(changed_file);
  bool found = false;
  for (const auto& prefab : prefabs) {
    if (prefab.prefab_id == 1086324737) {
      assert(prefab.name == renamed);
      found = true;
    }
  }
  assert(found);

  const auto validation = opengil::validate_gil(changed_file);
  assert(validation.ok);

  return 0;
}
