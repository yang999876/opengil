#include "test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/ui_pixel_import_ops.hpp"
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

std::vector<uint8_t> controller_entry(uint64_t controller_id, std::initializer_list<uint64_t> child_ids) {
  return message({
      varint_field(501, controller_id),
      len_field(503, packed_ids(child_ids)),
      len_field(12, message({len_field(501, string_bytes("controller"))})),
  });
}

opengil::GilFile make_file() {
  const auto top9 = message({
      len_field(502, controller_entry(1073741855, {})),
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
  file.path = "synthetic-ui-pixel-import.gil";
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

std::filesystem::path write_test_png() {
  const uint8_t bytes[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00,
      0x18, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
      0x9f, 0xe1, 0x3f, 0x43, 0x03, 0x03, 0x03, 0xc3, 0x7f, 0x10, 0x60, 0x00,
      0x00, 0x43, 0xd3, 0x08, 0x79, 0x4a, 0x4a, 0xe4, 0x46, 0x00, 0x00, 0x00,
      0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
  };
  const auto path = std::filesystem::temp_directory_path() / "opengil-ui-pixel-import-2x2.png";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(std::size(bytes)));
  return path;
}

bool throws_for_non_png(const opengil::GilFile& file) {
  const auto path = std::filesystem::temp_directory_path() / "opengil-ui-pixel-import-not-png.txt";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << "not png";
  out.close();
  try {
    opengil::UiPixelImportOptions options;
    options.pixel_size = 2.0;
    options.target_controller_entry_id = 1073741855;
    opengil::import_pixel_png_as_ui_primitives(file, path, options);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  const auto file = make_file();
  const auto png_path = write_test_png();

  opengil::UiPixelImportOptions options;
  options.pixel_size = 8.0;
  options.target_controller_entry_id = 1073741855;
  const auto mutation = opengil::import_pixel_png_as_ui_primitives(file, png_path, options);
  const auto changed = mutated_file(file, mutation);
  OPENGIL_CHECK(opengil::validate_gil(changed).ok);

  const auto root_assets = opengil::list_ui_assets(changed, 1073741855);
  OPENGIL_CHECK(root_assets.assets.size() == 1);
  OPENGIL_CHECK(root_assets.assets[0].kind == "group");
  OPENGIL_CHECK(root_assets.assets[0].name == "opengil-ui-pixel-import-2x2");
  OPENGIL_CHECK(root_assets.assets[0].transform.position.x == 4.0);
  OPENGIL_CHECK(root_assets.assets[0].transform.position.y == 4.0);
  OPENGIL_CHECK(root_assets.assets[0].transform.size.x == 16.0);
  OPENGIL_CHECK(root_assets.assets[0].transform.size.y == 16.0);
  OPENGIL_CHECK(root_assets.assets[0].mask_size.x == 16.0);
  OPENGIL_CHECK(root_assets.assets[0].mask_size.y == 16.0);
  OPENGIL_CHECK(root_assets.assets[0].entry_id.has_value());

  const auto pixels = opengil::list_ui_assets(changed, *root_assets.assets[0].entry_id);
  OPENGIL_CHECK(pixels.assets.size() == 3);
  OPENGIL_CHECK(pixels.assets[0].name == "pixel_0_0");
  OPENGIL_CHECK(pixels.assets[0].color == -65536);
  OPENGIL_CHECK(pixels.assets[0].transform.position.x == -4.0);
  OPENGIL_CHECK(pixels.assets[0].transform.position.y == 4.0);
  OPENGIL_CHECK(pixels.assets[0].transform.size.x == 8.0);
  OPENGIL_CHECK(pixels.assets[0].transform.size.y == 8.0);
  OPENGIL_CHECK(pixels.assets[1].name == "pixel_1_0");
  OPENGIL_CHECK(pixels.assets[1].color == -2147418368);
  OPENGIL_CHECK(pixels.assets[1].transform.position.x == 4.0);
  OPENGIL_CHECK(pixels.assets[1].transform.position.y == 4.0);
  OPENGIL_CHECK(pixels.assets[2].name == "pixel_0_1");
  OPENGIL_CHECK(pixels.assets[2].color == -16776961);
  OPENGIL_CHECK(pixels.assets[2].transform.position.x == -4.0);
  OPENGIL_CHECK(pixels.assets[2].transform.position.y == -4.0);
  OPENGIL_CHECK(mutation.summary.kind == "importPixelPngUiAssetGroup");
  OPENGIL_CHECK(mutation.summary.primitive_count == 1);
  OPENGIL_CHECK((mutation.summary.changed_top_fields == std::vector<uint32_t>{9}));
  OPENGIL_CHECK(throws_for_non_png(file));

  opengil::UiPixelImportOptions fixture_options;
  fixture_options.pixel_size = 8.0;
  fixture_options.target_controller_entry_id = 1073741855;
  const auto fixture_mutation = opengil::import_pixel_png_as_ui_primitives(
      make_file(),
      std::filesystem::path(OPENGIL_TEST_DIR) / "test.png",
      fixture_options);
  const auto fixture_changed = mutated_file(make_file(), fixture_mutation);
  OPENGIL_CHECK(opengil::validate_gil(fixture_changed).ok);

  const auto fixture_root_assets = opengil::list_ui_assets(fixture_changed, 1073741855);
  OPENGIL_CHECK(fixture_root_assets.assets.size() == 1);
  OPENGIL_CHECK(fixture_root_assets.assets[0].kind == "group");
  OPENGIL_CHECK(fixture_root_assets.assets[0].name == "test");
  OPENGIL_CHECK(fixture_root_assets.assets[0].transform.position.x == 60.0);
  OPENGIL_CHECK(fixture_root_assets.assets[0].transform.position.y == 112.0);
  OPENGIL_CHECK(fixture_root_assets.assets[0].transform.size.x == 96.0);
  OPENGIL_CHECK(fixture_root_assets.assets[0].transform.size.y == 104.0);
  OPENGIL_CHECK(fixture_root_assets.assets[0].mask_size.x == 96.0);
  OPENGIL_CHECK(fixture_root_assets.assets[0].mask_size.y == 104.0);
  OPENGIL_CHECK(fixture_root_assets.assets[0].entry_id.has_value());

  const auto fixture_pixels = opengil::list_ui_assets(fixture_changed, *fixture_root_assets.assets[0].entry_id);
  OPENGIL_CHECK(fixture_pixels.assets.size() == 132);
  OPENGIL_CHECK(fixture_pixels.assets[0].name == "pixel_3_2");
  OPENGIL_CHECK(fixture_pixels.assets[0].transform.position.x == -36.0);
  OPENGIL_CHECK(fixture_pixels.assets[0].transform.position.y == 48.0);
  OPENGIL_CHECK(fixture_pixels.assets.back().name == "pixel_13_14");
  OPENGIL_CHECK(fixture_pixels.assets.back().transform.position.x == 44.0);
  OPENGIL_CHECK(fixture_pixels.assets.back().transform.position.y == -48.0);

  return 0;
}
