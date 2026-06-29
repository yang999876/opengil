#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/model_ops.hpp"
#include "opengil/pixel_decoration_import_ops.hpp"
#include "opengil/semantic.hpp"
#include "opengil/wire.hpp"

#ifndef OPENGIL_TEST_FIXTURE_DIR
#define OPENGIL_TEST_FIXTURE_DIR "tests/fixtures"
#endif

namespace {

std::vector<uint8_t> packed_refs_at_path(std::span<const uint8_t> message, std::span<const uint32_t> path) {
  const auto fields = opengil::parse_owned_fields(message);
  for (const auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) return field.data;
    return packed_refs_at_path(field.data, path.subspan(1));
  }
  OPENGIL_CHECK(false);
  return {};
}

std::vector<uint64_t> decode_packed(std::span<const uint8_t> data) {
  std::vector<uint64_t> out;
  size_t offset = 0;
  while (offset < data.size()) {
    const auto value = opengil::read_varint(data, offset);
    OPENGIL_CHECK(value);
    out.push_back(value->value);
    offset = value->next;
  }
  return out;
}

std::vector<uint8_t> top4_entry_by_prefab_id(const opengil::GilFile& file, uint64_t prefab_id) {
  const auto top4 = opengil::top_level_data(file, 4);
  OPENGIL_CHECK(top4);
  const std::array<uint32_t, 1> id_path{1};
  for (const auto& field : opengil::len_fields(*top4, 1)) {
    const auto entry = opengil::field_data(*top4, field);
    if (opengil::read_varint_at_path(entry, id_path) == prefab_id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  OPENGIL_CHECK(false);
  return {};
}

opengil::GilFile load_mutation_as_file(const opengil::PixelDecorationImportMutation& mutation) {
  const auto path = std::filesystem::temp_directory_path() / "opengil-test-pixel-decoration-import.gil";
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

}  // namespace

int main() {
  const auto input = std::filesystem::path(OPENGIL_TEST_FIXTURE_DIR) / "test1.gil";
  const auto png = std::filesystem::path(OPENGIL_TEST_FIXTURE_DIR) / "pixel-2x2.png";
  const auto file = opengil::load_gil_file(input);

  opengil::PixelDecorationImportOptions options;
  options.prefab_id = 1077939000;
  options.asset_id = 20001220;
  options.pixel_size = 0.25;
  const auto mutation = opengil::import_pixel_png_as_decoration_prefab(file, png, options);

  OPENGIL_CHECK(mutation.summary.prefab_id == options.prefab_id);
  OPENGIL_CHECK(mutation.summary.asset_id == options.asset_id);
  OPENGIL_CHECK(mutation.summary.source_pixel_count == 4);
  OPENGIL_CHECK(mutation.summary.decoration_count == 3);
  OPENGIL_CHECK(mutation.summary.prefab_decoration_ids.size() == 3);

  const auto changed = load_mutation_as_file(mutation);
  OPENGIL_CHECK(opengil::validate_gil(changed).ok);

  const auto prefabs = opengil::list_prefabs(changed);
  bool found = false;
  for (const auto& prefab : prefabs) {
    if (prefab.prefab_id == options.prefab_id) {
      found = true;
      OPENGIL_CHECK(prefab.model_asset_id == opengil::EMPTY_MODEL_ASSET_ID);
      break;
    }
  }
  OPENGIL_CHECK(found);

  const auto prefab_entry = top4_entry_by_prefab_id(changed, options.prefab_id);
  const std::array<uint32_t, 2> refs_path{6, 50};
  const auto refs = decode_packed(packed_refs_at_path(prefab_entry, refs_path));
  for (const auto id : mutation.summary.prefab_decoration_ids) {
    OPENGIL_CHECK(std::find(refs.begin(), refs.end(), id) != refs.end());
  }

  return 0;
}
