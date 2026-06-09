#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/nodegraph_ops.hpp"
#include "opengil/semantic.hpp"
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

opengil::GilFile make_synthetic_file() {
  constexpr uint64_t prefab_id = 1086324737;
  constexpr uint64_t nodegraph_id = 1073741913;

  const auto definition_id = message({varint_field(5, nodegraph_id)});
  const auto definition = message({
      len_field(1, definition_id),
      len_field(2, std::vector<uint8_t>{'T', 'e', 's', 't', '.', 't', 's'}),
  });
  const auto top10 = message({len_field(1, message({len_field(1, definition)}))});

  const auto prefab_carrier = message({varint_field(1, 3)});
  const auto prefab = message({
      varint_field(1, prefab_id),
      len_field(7, prefab_carrier),
  });
  const auto top4 = message({len_field(1, prefab)});

  const auto scene_prefab_ref = message({varint_field(1, prefab_id)});
  const auto scene_carrier = message({varint_field(1, 3)});
  const auto scene = message({
      len_field(2, scene_prefab_ref),
      len_field(6, scene_carrier),
  });
  const auto top5 = message({len_field(1, scene)});

  const auto payload = message({
      len_field(4, top4),
      len_field(5, top5),
      len_field(10, top10),
  });

  opengil::GilHeader header;
  header.schema = 1;
  header.head_tag = 0;
  header.file_type = 0;
  header.tail_tag = 0;

  opengil::GilFile file;
  file.path = "synthetic.gil";
  file.header = header;
  file.bytes = opengil::build_gil_bytes(header, payload);
  return file;
}

opengil::GilFile load_mutation_as_file(const opengil::NodegraphMutation& mutation, const char* name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
  stream.close();
  return opengil::load_gil_file(path);
}

}  // namespace

int main() {
  constexpr uint64_t prefab_id = 1086324737;
  constexpr uint64_t nodegraph_id = 1073741913;

  const auto file = make_synthetic_file();
  const auto before_graphs = opengil::list_nodegraphs(file);
  assert(before_graphs.size() == 1);
  assert(before_graphs[0].role == "definition");
  assert(before_graphs[0].id == nodegraph_id);

  const auto mutation = opengil::attach_nodegraph_to_prefab(file, prefab_id, nodegraph_id);
  assert(mutation.summary.prefab_updated);
  assert(!mutation.summary.already_attached);
  assert(mutation.summary.scene_updated == 1);
  assert(mutation.summary.preview_updated == 0);
  assert(mutation.summary.changed_top_fields.size() == 2);
  assert(mutation.summary.changed_top_fields[0] == 4);
  assert(mutation.summary.changed_top_fields[1] == 5);

  const auto changed_file = load_mutation_as_file(mutation, "opengil-test-nodegraph.gil");
  const auto validation = opengil::validate_gil(changed_file);
  assert(validation.ok);

  const auto after_graphs = opengil::list_nodegraphs(changed_file);
  size_t reference_count = 0;
  for (const auto& graph : after_graphs) {
    if (graph.role == "reference" && graph.id == nodegraph_id) reference_count++;
  }
  assert(reference_count >= 2);

  const auto repeated = opengil::attach_nodegraph_to_prefab(changed_file, prefab_id, nodegraph_id);
  assert(repeated.summary.already_attached);
  assert(repeated.summary.changed_top_fields.empty());

  return 0;
}
