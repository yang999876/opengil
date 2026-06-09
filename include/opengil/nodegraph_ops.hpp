#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "opengil/gil.hpp"

namespace opengil {

struct AttachNodegraphSummary {
  uint64_t prefab_id = 0;
  uint64_t nodegraph_id = 0;
  std::string nodegraph_name;
  bool prefab_updated = false;
  bool already_attached = false;
  size_t scene_updated = 0;
  size_t preview_updated = 0;
  std::vector<uint32_t> changed_top_fields;
};

struct AttachAllNodegraphsSummary {
  uint64_t prefab_id = 0;
  size_t available_count = 0;
  size_t attached_count = 0;
  std::vector<uint64_t> attached_nodegraph_ids;
  std::vector<AttachNodegraphSummary> items;
  std::vector<uint32_t> changed_top_fields;
};

struct NodegraphMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  AttachNodegraphSummary summary;
};

struct AttachAllNodegraphsMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  AttachAllNodegraphsSummary summary;
};

NodegraphMutation attach_nodegraph_to_prefab(const GilFile& file, uint64_t prefab_id, uint64_t nodegraph_id);
AttachAllNodegraphsMutation attach_all_nodegraphs_to_prefab(const GilFile& file, uint64_t prefab_id);

std::string attach_nodegraph_summary_to_json(const AttachNodegraphSummary& summary);
std::string attach_all_nodegraphs_summary_to_json(const AttachAllNodegraphsSummary& summary);

}  // namespace opengil
