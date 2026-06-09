#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "opengil/gil.hpp"
#include "opengil/model_ops.hpp"
#include "opengil/semantic.hpp"

namespace {

std::filesystem::path fixture_path(const char* name) {
  return std::filesystem::path(OPENGIL_TEST_FIXTURE_DIR) / name;
}

opengil::GilFile load_mutation_as_file(const opengil::GilMutation& mutation, const char* name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

}  // namespace

int main() {
  const auto file = opengil::load_gil_file(fixture_path("test1.gil"));

  const auto changed = opengil::set_prefab_model_asset_id(file, 1086324737, 20001220);
  assert(changed.model_summary.prefab_updated);
  assert(changed.model_summary.scene_updated == 9);
  assert(changed.model_summary.preview_updated == 0);

  const auto changed_file = load_mutation_as_file(changed, "opengil-test-model.gil");
  const auto changed_model = opengil::get_model_info(changed_file, 1086324737);
  assert(changed_model);
  assert(changed_model->prefab_model_asset_id == 20001220);
  assert(changed_model->scene_model_asset_ids.size() == 9);
  for (const auto value : changed_model->scene_model_asset_ids) {
    assert(value == 20001220);
  }

  const auto empty = opengil::set_prefab_to_empty_model(file, 1086324737);
  const auto empty_file = load_mutation_as_file(empty, "opengil-test-empty.gil");
  const auto empty_model = opengil::get_model_info(empty_file, 1086324737);
  assert(empty_model);
  assert(empty_model->prefab_model_asset_id == opengil::EMPTY_MODEL_ASSET_ID);
  for (const auto value : empty_model->scene_model_asset_ids) {
    assert(value == opengil::EMPTY_MODEL_ASSET_ID);
  }

  return 0;
}

