#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
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

uint32_t crc32(std::span<const uint8_t> data) {
  uint32_t crc = 0xffffffffu;
  for (uint8_t byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
    }
  }
  return crc ^ 0xffffffffu;
}

uint32_t adler32(std::span<const uint8_t> data) {
  constexpr uint32_t mod = 65521;
  uint32_t a = 1;
  uint32_t b = 0;
  for (uint8_t byte : data) {
    a = (a + byte) % mod;
    b = (b + a) % mod;
  }
  return (b << 16u) | a;
}

void append_u32_be(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<uint8_t>(value & 0xffu));
}

void append_chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
  append_u32_be(out, static_cast<uint32_t>(data.size()));
  const size_t chunk_start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  append_u32_be(out, crc32(std::span<const uint8_t>(out.data() + chunk_start, out.size() - chunk_start)));
}

std::vector<uint8_t> zlib_uncompressed(std::span<const uint8_t> data) {
  std::vector<uint8_t> out{0x78, 0x01};
  size_t offset = 0;
  while (offset < data.size()) {
    const size_t block_size = std::min<size_t>(65535, data.size() - offset);
    const bool final_block = offset + block_size == data.size();
    out.push_back(final_block ? 0x01 : 0x00);
    out.push_back(static_cast<uint8_t>(block_size & 0xffu));
    out.push_back(static_cast<uint8_t>((block_size >> 8u) & 0xffu));
    const uint16_t nlen = static_cast<uint16_t>(~static_cast<uint16_t>(block_size));
    out.push_back(static_cast<uint8_t>(nlen & 0xffu));
    out.push_back(static_cast<uint8_t>((nlen >> 8u) & 0xffu));
    out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + block_size));
    offset += block_size;
  }
  append_u32_be(out, adler32(data));
  return out;
}

std::filesystem::path write_rgba_png(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& rgba) {
  OPENGIL_CHECK(rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

  std::vector<uint8_t> png{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

  std::vector<uint8_t> ihdr;
  append_u32_be(ihdr, width);
  append_u32_be(ihdr, height);
  ihdr.push_back(8);
  ihdr.push_back(6);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  append_chunk(png, "IHDR", ihdr);

  std::vector<uint8_t> scanlines;
  const size_t stride = static_cast<size_t>(width) * 4;
  for (uint32_t y = 0; y < height; ++y) {
    scanlines.push_back(0);
    const size_t offset = static_cast<size_t>(y) * stride;
    scanlines.insert(scanlines.end(), rgba.begin() + static_cast<std::ptrdiff_t>(offset), rgba.begin() + static_cast<std::ptrdiff_t>(offset + stride));
  }

  append_chunk(png, "IDAT", zlib_uncompressed(scanlines));
  append_chunk(png, "IEND", {});

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  return path;
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

std::vector<uint8_t> top27_prefab_decoration_by_id(const opengil::GilFile& file, uint64_t decoration_id) {
  const auto top27 = opengil::top_level_data(file, 27);
  OPENGIL_CHECK(top27);
  const std::array<uint32_t, 1> id_path{1};
  for (const auto& field : opengil::len_fields(*top27, 1)) {
    const auto entry = opengil::field_data(*top27, field);
    if (opengil::read_varint_at_path(entry, id_path) == decoration_id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  OPENGIL_CHECK(false);
  return {};
}

template <size_t N>
std::optional<float> fixed32_at_path(const std::vector<uint8_t>& message, const std::array<uint32_t, N>& path) {
  return opengil::read_fixed32_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
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

  const auto merged_png = write_rgba_png(
      std::filesystem::temp_directory_path() / "opengil-test-pixel-decoration-merged.png",
      3,
      2,
      {
          255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 255, 255,
          255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 0, 0,
      });

  options.prefab_id = 1077939001;
  const auto merged_mutation = opengil::import_pixel_png_as_decoration_prefab(file, merged_png, options);
  OPENGIL_CHECK(merged_mutation.summary.source_pixel_count == 6);
  OPENGIL_CHECK(merged_mutation.summary.decoration_count == 2);
  OPENGIL_CHECK(merged_mutation.summary.prefab_decoration_ids.size() == 2);

  const auto merged_changed = load_mutation_as_file(merged_mutation);
  OPENGIL_CHECK(opengil::validate_gil(merged_changed).ok);

  const auto first_decoration = top27_prefab_decoration_by_id(merged_changed, merged_mutation.summary.prefab_decoration_ids[0]);
  const std::array<uint32_t, 4> pos_x_path{5, 11, 1, 1};
  const std::array<uint32_t, 4> pos_z_path{5, 11, 1, 3};
  const std::array<uint32_t, 4> scale_x_path{5, 11, 3, 1};
  const std::array<uint32_t, 4> scale_z_path{5, 11, 3, 3};
  OPENGIL_CHECK(fixed32_at_path(first_decoration, pos_x_path) == 0.125f);
  OPENGIL_CHECK(fixed32_at_path(first_decoration, pos_z_path) == 0.125f);
  OPENGIL_CHECK(fixed32_at_path(first_decoration, scale_x_path) == 0.5f);
  OPENGIL_CHECK(fixed32_at_path(first_decoration, scale_z_path) == 0.5f);

  options.prefab_id = 1077939002;
  options.merge_same_color_rects = false;
  const auto unmerged_mutation = opengil::import_pixel_png_as_decoration_prefab(file, merged_png, options);
  OPENGIL_CHECK(!unmerged_mutation.summary.merge_same_color_rects);
  OPENGIL_CHECK(unmerged_mutation.summary.source_pixel_count == 6);
  OPENGIL_CHECK(unmerged_mutation.summary.decoration_count == 5);
  OPENGIL_CHECK(unmerged_mutation.summary.prefab_decoration_ids.size() == 5);

  return 0;
}
