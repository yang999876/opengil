#include <cassert>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#include "opengil/decoration_ops.hpp"
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

std::vector<uint8_t> message(std::initializer_list<opengil::OwnedField> fields) {
  return opengil::rebuild_message(std::vector<opengil::OwnedField>(fields));
}

std::vector<uint8_t> packed_varints(std::initializer_list<uint64_t> values) {
  std::vector<uint8_t> out;
  for (uint64_t value : values) {
    const auto encoded = opengil::encode_varint(value);
    out.insert(out.end(), encoded.begin(), encoded.end());
  }
  return out;
}

std::vector<uint64_t> decode_packed(std::span<const uint8_t> data) {
  std::vector<uint64_t> values;
  size_t offset = 0;
  while (offset < data.size()) {
    const auto value = opengil::read_varint(data, offset);
    assert(value);
    values.push_back(value->value);
    offset = value->next;
  }
  return values;
}

opengil::GilFile make_file() {
  const auto top4 = message({
      len_field(1, message({
          varint_field(1, 101),
          len_field(6, message({
              len_field(50, packed_varints({0, 5, 1001})),
          })),
      })),
  });
  const auto top8 = message({
      len_field(1, message({
          varint_field(1, 201),
          len_field(2, message({varint_field(1, 101)})),
          len_field(5, message({
              len_field(50, packed_varints({0, 0})),
          })),
      })),
  });
  const auto top27 = message({
      len_field(1, message({varint_field(1, 1001)})),
      len_field(2, message({varint_field(1, 1002)})),
  });
  const auto payload = message({
      len_field(4, top4),
      len_field(8, top8),
      len_field(27, top27),
  });

  opengil::GilHeader header;
  header.schema = 1;

  opengil::GilFile file;
  file.path = "synthetic-decoration.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

template <typename Mutation>
opengil::GilFile load_mutation_as_file(const Mutation& mutation, const char* name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

std::vector<uint8_t> entry_by_id(const opengil::GilFile& file, uint32_t top_field, uint64_t id) {
  const auto top = opengil::top_level_data(file, top_field);
  assert(top);
  const std::array<uint32_t, 1> id_path{1};
  for (const auto& field : opengil::len_fields(*top, 1)) {
    const auto entry = opengil::field_data(*top, field);
    if (opengil::read_varint_at_path(entry, id_path) == id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  assert(false);
  return {};
}

std::vector<uint8_t> bytes_at_path(std::span<const uint8_t> message, std::span<const uint32_t> path) {
  const auto fields = opengil::parse_owned_fields(message);
  for (const auto& field : fields) {
    if (field.number != path[0]) continue;
    if (path.size() == 1) return field.data;
    return bytes_at_path(field.data, path.subspan(1));
  }
  assert(false);
  return {};
}

size_t len_count(const opengil::GilFile& file, uint32_t top_field, uint32_t repeated_field) {
  const auto top = opengil::top_level_data(file, top_field);
  assert(top);
  return opengil::len_fields(*top, repeated_field).size();
}

}  // namespace

int main() {
  const auto file = make_file();
  opengil::DecorationSpec spec;
  spec.asset_id = 20001220;
  spec.name = "Deco";
  spec.transform.position = {1.0, 2.0, 3.0};
  spec.transform.rotation = {4.0, 5.0, 6.0};
  spec.transform.scale = {0.3, 0.4, 0.5};

  const auto mutation = opengil::add_prefab_decorations(file, 101, {spec});
  assert(mutation.summary.prefab_id == 101);
  assert(mutation.summary.scene_instance_count == 1);
  assert(mutation.summary.prefab_decoration_ids.size() == 1);
  assert(mutation.summary.prefab_decoration_ids[0] == 1003);
  assert(mutation.summary.scene_decoration_ids.size() == 1);
  assert(mutation.summary.scene_decoration_ids[0] == 1004);
  assert(mutation.summary.changed_top_fields.size() == 3);

  const auto changed = load_mutation_as_file(mutation, "opengil-test-decoration-add.gil");
  assert(opengil::validate_gil(changed).ok);

  const auto prefab_entry = entry_by_id(changed, 4, 101);
  const std::array<uint32_t, 2> prefab_refs_path{6, 50};
  const auto prefab_refs = decode_packed(bytes_at_path(prefab_entry, prefab_refs_path));
  assert((prefab_refs == std::vector<uint64_t>{0, 10, 1001, 1003}));

  const auto scene_entry = entry_by_id(changed, 8, 201);
  const std::array<uint32_t, 2> scene_refs_path{5, 50};
  const auto scene_refs = decode_packed(bytes_at_path(scene_entry, scene_refs_path));
  assert((scene_refs == std::vector<uint64_t>{0, 5, 1004}));

  assert(len_count(changed, 27, 1) == 2);
  assert(len_count(changed, 27, 2) == 2);

  return 0;
}
