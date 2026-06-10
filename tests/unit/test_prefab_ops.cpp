#include "test_support.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/prefab_ops.hpp"
#include "opengil/semantic.hpp"
#include "opengil/wire.hpp"

namespace {

std::filesystem::path fixture_path(const char* name) {
  return std::filesystem::path(OPENGIL_TEST_FIXTURE_DIR) / name;
}

template <typename Mutation>
opengil::GilFile load_mutation_as_file(const Mutation& mutation, const char* name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

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

std::vector<uint8_t> bytes_from_string(const char* text) {
  return std::vector<uint8_t>(text, text + std::char_traits<char>::length(text));
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

std::vector<uint8_t> prefab_entry(uint64_t prefab_id, uint64_t decoration_id) {
  const auto name_group = message({len_field(1, bytes_from_string("Source Prefab"))});
  const auto carrier = message({
      len_field(11, name_group),
      len_field(50, packed_varints({0, 5, decoration_id})),
  });
  const auto preview = message({
      len_field(11, message({
          len_field(1, message({
              fixed32_field(1, 1.0f),
              fixed32_field(3, 2.0f),
          })),
      })),
  });
  return message({
      varint_field(1, prefab_id),
      varint_field(2, 20001220),
      len_field(6, carrier),
      len_field(7, preview),
  });
}

std::vector<uint8_t> prefab_mapping(uint64_t prefab_id) {
  return message({
      varint_field(1, 100),
      varint_field(2, prefab_id),
  });
}

std::vector<uint8_t> decoration_record(uint64_t decoration_id, uint64_t owner_prefab_id) {
  return message({
      varint_field(1, decoration_id),
      len_field(4, message({
          len_field(50, message({
              varint_field(502, owner_prefab_id),
          })),
      })),
  });
}

opengil::GilFile make_clone_synthetic_file() {
  constexpr uint64_t source_prefab_id = 101;
  constexpr uint64_t decoration_id = 1001;

  const auto top4 = message({
      len_field(1, prefab_entry(source_prefab_id, decoration_id)),
  });
  const auto tab = message({
      len_field(1, bytes_from_string("Balls")),
      varint_field(3, 6),
      len_field(5, prefab_mapping(source_prefab_id)),
  });
  const auto root = message({len_field(4, tab)});
  const auto category6 = message({
      varint_field(1, 6),
      len_field(2, root),
  });
  const auto unclassified = message({
      len_field(1, bytes_from_string("Unclassified")),
      varint_field(3, 2),
      len_field(5, prefab_mapping(source_prefab_id)),
  });
  const auto category3 = message({
      varint_field(1, 3),
      len_field(3, unclassified),
  });
  const auto top6 = message({
      len_field(1, category6),
      len_field(1, category3),
  });
  const auto top27 = message({
      len_field(1, decoration_record(decoration_id, source_prefab_id)),
  });
  const auto top10 = message({
      len_field(1, message({varint_field(1, source_prefab_id)})),
      len_field(1, message({varint_field(1, 999)})),
  });
  const auto payload = message({
      len_field(4, top4),
      len_field(6, top6),
      len_field(10, top10),
      len_field(27, top27),
  });

  opengil::GilHeader header;
  header.schema = 1;

  opengil::GilFile file;
  file.path = "synthetic-clone-prefab.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

bool has_decoration_owner(const opengil::GilFile& file, uint64_t decoration_id, uint64_t owner_prefab_id) {
  const auto top27 = opengil::top_level_data(file, 27);
  OPENGIL_CHECK(top27);
  const std::array<uint32_t, 1> id_path{1};
  const std::array<uint32_t, 3> owner_path{4, 50, 502};
  for (const auto& field : opengil::len_fields(*top27, 1)) {
    const auto item = opengil::field_data(*top27, field);
    if (opengil::read_varint_at_path(item, id_path) == decoration_id &&
        opengil::read_varint_at_path(item, owner_path) == owner_prefab_id) {
      return true;
    }
  }
  return false;
}

size_t len_field_count(const opengil::GilFile& file, uint32_t top_field_number, uint32_t repeated_field_number) {
  const auto top = opengil::top_level_data(file, top_field_number);
  OPENGIL_CHECK(top);
  return opengil::len_fields(*top, repeated_field_number).size();
}

}  // namespace

int main() {
  const auto file = opengil::load_gil_file(fixture_path("test1.gil"));

  const std::string renamed = "openGil Rename Test";
  const auto mutation = opengil::rename_prefab(file, 1086324737, renamed);
  OPENGIL_CHECK(mutation.summary.prefab_id == 1086324737);
  OPENGIL_CHECK(mutation.summary.before_name == "Default Template");
  OPENGIL_CHECK(mutation.summary.after_name == renamed);
  OPENGIL_CHECK(mutation.summary.changed_top_fields.size() == 1);
  OPENGIL_CHECK(mutation.summary.changed_top_fields[0] == 4);

  const auto changed_file = load_mutation_as_file(mutation, "opengil-test-rename.gil");
  const auto prefabs = opengil::list_prefabs(changed_file);
  bool found = false;
  for (const auto& prefab : prefabs) {
    if (prefab.prefab_id == 1086324737) {
      OPENGIL_CHECK(prefab.name == renamed);
      found = true;
    }
  }
  OPENGIL_CHECK(found);

  const auto validation = opengil::validate_gil(changed_file);
  OPENGIL_CHECK(validation.ok);

  const auto clone_file = make_clone_synthetic_file();
  const auto cloned = opengil::clone_prefab_into_tab_by_id(clone_file, 101, 6, "Clone Prefab");
  OPENGIL_CHECK(cloned.summary.source_prefab_id == 101);
  OPENGIL_CHECK(cloned.summary.source_name == "Source Prefab");
  OPENGIL_CHECK(cloned.summary.new_prefab_id == 102);
  OPENGIL_CHECK(cloned.summary.new_prefab_name == "Clone Prefab");
  OPENGIL_CHECK(cloned.summary.target_tab_id == 6);
  OPENGIL_CHECK(cloned.summary.target_tab_name == "Balls");
  OPENGIL_CHECK(cloned.summary.cloned_decoration_count == 1);
  OPENGIL_CHECK(cloned.summary.changed_top_fields.size() == 3);
  OPENGIL_CHECK(cloned.summary.changed_top_fields[0] == 4);
  OPENGIL_CHECK(cloned.summary.changed_top_fields[1] == 6);
  OPENGIL_CHECK(cloned.summary.changed_top_fields[2] == 27);

  const auto cloned_file = load_mutation_as_file(cloned, "opengil-test-clone-prefab.gil");
  OPENGIL_CHECK(opengil::validate_gil(cloned_file).ok);
  const auto cloned_prefabs = opengil::list_prefabs(cloned_file);
  bool found_clone = false;
  for (const auto& prefab : cloned_prefabs) {
    if (prefab.prefab_id == 102) {
      OPENGIL_CHECK(prefab.name == "Clone Prefab");
      found_clone = true;
    }
  }
  OPENGIL_CHECK(found_clone);

  const auto clone_tabs = opengil::list_prefab_tabs(cloned_file, 102);
  OPENGIL_CHECK(clone_tabs.size() == 1);
  OPENGIL_CHECK(clone_tabs[0].id == 6);
  OPENGIL_CHECK(clone_tabs[0].name == "Balls");
  OPENGIL_CHECK(has_decoration_owner(cloned_file, 1002, 102));

  const auto copied = opengil::copy_prefab_to_tab_by_id(clone_file, 101, 6);
  OPENGIL_CHECK(copied.summary.source_prefab_id == 101);
  OPENGIL_CHECK(copied.summary.source_name == "Source Prefab");
  OPENGIL_CHECK(copied.summary.new_prefab_id == 102);
  OPENGIL_CHECK(copied.summary.new_prefab_name == "Source Prefab-copy");
  OPENGIL_CHECK(copied.summary.target_tab_id == 6);
  OPENGIL_CHECK(copied.summary.cloned_decoration_count == 1);

  const auto copied_file = load_mutation_as_file(copied, "opengil-test-copy-prefab-to-tab.gil");
  OPENGIL_CHECK(opengil::validate_gil(copied_file).ok);
  const auto copied_prefabs = opengil::list_prefabs(copied_file);
  bool found_copy = false;
  for (const auto& prefab : copied_prefabs) {
    if (prefab.prefab_id == 102) {
      OPENGIL_CHECK(prefab.name == "Source Prefab-copy");
      found_copy = true;
    }
  }
  OPENGIL_CHECK(found_copy);
  OPENGIL_CHECK(has_decoration_owner(copied_file, 1002, 102));

  const auto deleted = opengil::delete_prefab(clone_file, 101);
  OPENGIL_CHECK(deleted.summary.prefab_id == 101);
  OPENGIL_CHECK(deleted.summary.removed_decoration_ids.size() == 1);
  OPENGIL_CHECK(deleted.summary.removed_decoration_ids[0] == 1001);
  OPENGIL_CHECK(deleted.summary.changed_top_fields.size() == 4);

  const auto deleted_file = load_mutation_as_file(deleted, "opengil-test-delete-prefab.gil");
  OPENGIL_CHECK(opengil::validate_gil(deleted_file).ok);
  const auto remaining_prefabs = opengil::list_prefabs(deleted_file);
  for (const auto& prefab : remaining_prefabs) {
    OPENGIL_CHECK(prefab.prefab_id != 101);
  }
  OPENGIL_CHECK(opengil::list_prefab_tabs(deleted_file, 101).empty());
  OPENGIL_CHECK(!has_decoration_owner(deleted_file, 1001, 101));
  OPENGIL_CHECK(len_field_count(deleted_file, 10, 1) == 1);

  return 0;
}
