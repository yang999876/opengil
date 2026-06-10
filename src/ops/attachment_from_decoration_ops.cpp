#include "opengil/attachment_from_decoration_ops.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <regex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "opengil/json.hpp"
#include "opengil/wire.hpp"

namespace opengil {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

struct DecorationInfo {
  std::string name;
  double x = 0.0;
  double y = 0.0;
  double rot_z = 0.0;
  double scale_y = 0.0;
};

template <size_t N>
std::optional<uint64_t> read_varint_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_varint_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

template <size_t N>
std::optional<std::string> read_string_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_string_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

double round6(double value) {
  return std::round(value * 1000000.0) / 1000000.0;
}

template <size_t N>
double read_float_path_or_zero(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  const auto value = read_fixed32_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
  return value ? round6(static_cast<double>(*value)) : 0.0;
}

std::string utf8_right_hand() {
  return "\xE5\x8F\xB3\xE6\x89\x8B";
}

std::string utf8_left_hand() {
  return "\xE5\xB7\xA6\xE6\x89\x8B";
}

std::string utf8_head() {
  return "\xE5\xA4\xB4";
}

std::string utf8_display_prefix() {
  return "\xE8\x87\xAA\xE5\xAE\x9A\xE4\xB9\x89\xE6\x8C\x82\xE6\x8E\xA5\xE7\x82\xB9";
}

std::vector<uint8_t> find_prefab_entry(std::span<const uint8_t> top4, uint64_t prefab_id) {
  const std::array<uint32_t, 1> id_path{1};
  for (const auto& field : len_fields(top4, 1)) {
    const auto entry = field_data(top4, field);
    if (read_varint_path(entry, id_path) == prefab_id) {
      return std::vector<uint8_t>(entry.begin(), entry.end());
    }
  }
  return {};
}

std::vector<DecorationInfo> collect_decorations(std::span<const uint8_t> top27, uint64_t prefab_id) {
  std::vector<DecorationInfo> decorations;
  const std::array<uint32_t, 3> owner_path{4, 50, 502};
  const std::array<uint32_t, 3> name_path{4, 11, 1};
  const std::array<uint32_t, 4> x_path{5, 11, 1, 1};
  const std::array<uint32_t, 4> y_path{5, 11, 1, 2};
  const std::array<uint32_t, 4> rot_z_path{5, 11, 2, 3};
  const std::array<uint32_t, 4> scale_y_path{5, 11, 3, 2};

  for (const auto& field : len_fields(top27, 1)) {
    const auto entry = field_data(top27, field);
    if (read_varint_path(entry, owner_path) != prefab_id) continue;
    const auto name = read_string_path(entry, name_path);
    if (!name) continue;

    DecorationInfo decoration;
    decoration.name = *name;
    decoration.x = read_float_path_or_zero(entry, x_path);
    decoration.y = read_float_path_or_zero(entry, y_path);
    decoration.rot_z = read_float_path_or_zero(entry, rot_z_path);
    decoration.scale_y = read_float_path_or_zero(entry, scale_y_path);
    decorations.push_back(std::move(decoration));
  }
  return decorations;
}

std::optional<DecorationInfo> find_decoration(const std::vector<DecorationInfo>& decorations, const std::string& name) {
  for (const auto& decoration : decorations) {
    if (decoration.name == name) return decoration;
  }
  return std::nullopt;
}

std::optional<std::vector<uint8_t>> find_attachment21_entry(std::span<const uint8_t> prefab_entry, const std::string& name) {
  const std::array<uint32_t, 1> wrapper_tag_path{1};
  const std::array<uint32_t, 1> item_name_path{1};
  for (const auto& wrapper_field : len_fields(prefab_entry, 7)) {
    const auto wrapper = field_data(prefab_entry, wrapper_field);
    if (read_varint_path(wrapper, wrapper_tag_path) != 11) continue;
    const auto container_field = first_len_field(wrapper, 21);
    if (!container_field) return std::nullopt;
    const auto container = field_data(wrapper, *container_field);
    for (const auto& item_field : len_fields(container, 1)) {
      const auto item = field_data(container, item_field);
      if (read_string_path(item, item_name_path) == name) {
        return std::vector<uint8_t>(item.begin(), item.end());
      }
    }
  }
  return std::nullopt;
}

std::vector<int> collect_existing_display_indices(std::span<const uint8_t> prefab_entry) {
  std::vector<int> indices;
  const std::array<uint32_t, 1> wrapper_tag_path{1};
  const std::array<uint32_t, 1> display_name_path{505};
  const std::regex display_regex("^" + utf8_display_prefix() + "([0-9]+)$");

  for (const auto& wrapper_field : len_fields(prefab_entry, 7)) {
    const auto wrapper = field_data(prefab_entry, wrapper_field);
    if (read_varint_path(wrapper, wrapper_tag_path) != 11) continue;
    const auto container_field = first_len_field(wrapper, 21);
    if (!container_field) return indices;
    const auto container = field_data(wrapper, *container_field);
    for (const auto& item_field : len_fields(container, 1)) {
      const auto item = field_data(container, item_field);
      const auto display_name = read_string_path(item, display_name_path);
      if (!display_name) continue;
      std::smatch match;
      if (std::regex_match(*display_name, match, display_regex)) {
        indices.push_back(std::stoi(match[1].str()));
      }
    }
  }
  return indices;
}

bool contains_index(const std::vector<int>& indices, int value) {
  for (int index : indices) {
    if (index == value) return true;
  }
  return false;
}

std::vector<std::string> allocate_display_names(std::vector<int> existing_indices, size_t count) {
  std::vector<std::string> names;
  int next = 1;
  while (names.size() < count) {
    if (!contains_index(existing_indices, next)) {
      existing_indices.push_back(next);
      names.push_back(utf8_display_prefix() + std::to_string(next));
    }
    ++next;
  }
  return names;
}

std::vector<AttachmentPointSpec> derive_attachment_specs(std::span<const uint8_t> prefab_entry, const std::vector<DecorationInfo>& decorations) {
  const auto head = find_decoration(decorations, "Head");
  const auto left_elbow = find_decoration(decorations, "LElbow");
  if (!head || !left_elbow) throw std::runtime_error("required decorations Head / LElbow not found");

  const auto right_hand = find_attachment21_entry(prefab_entry, utf8_right_hand());
  const std::array<uint32_t, 2> rot_x_path{3, 1};
  const std::array<uint32_t, 2> rot_y_path{3, 2};
  const double right_hand_rot_x = right_hand ? read_float_path_or_zero(*right_hand, rot_x_path) : 0.0;
  const double right_hand_rot_y = right_hand ? read_float_path_or_zero(*right_hand, rot_y_path) : 0.0;

  const auto display_names = allocate_display_names(collect_existing_display_indices(prefab_entry), 2);
  const double length = left_elbow->scale_y * 5.0;
  const double angle = left_elbow->rot_z * kPi / 180.0;

  AttachmentPointSpec left_hand_spec;
  left_hand_spec.name = utf8_left_hand();
  left_hand_spec.display_name = display_names[0];
  left_hand_spec.x = round6(-left_elbow->x + std::sin(angle) * length);
  left_hand_spec.y = round6(left_elbow->y + std::cos(angle) * length);
  left_hand_spec.rot_x = right_hand_rot_x;
  left_hand_spec.rot_y = round6(-right_hand_rot_y);

  AttachmentPointSpec head_spec;
  head_spec.name = utf8_head();
  head_spec.display_name = display_names[1];
  head_spec.x = round6(head->x);
  head_spec.y = round6(head->y);
  head_spec.rot_x = 0.0;
  head_spec.rot_y = 0.0;

  return {left_hand_spec, head_spec};
}

std::string string_array_json(const std::vector<std::string>& values) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out << ",";
    out << json::quote(values[i]);
  }
  out << "]";
  return out.str();
}

}  // namespace

AttachmentMutation add_attachment_points_from_decorations(
    const GilFile& file,
    uint64_t prefab_id,
    std::optional<uint64_t> object_id) {
  const auto top4 = top_level_data(file, 4);
  const auto top27 = top_level_data(file, 27);
  if (!top4 || !top27) throw std::runtime_error("required top-level fields 4 and 27 not found");

  const auto prefab_entry = find_prefab_entry(*top4, prefab_id);
  if (prefab_entry.empty()) throw std::runtime_error("prefab id not found in top-level field 4");

  const auto decorations = collect_decorations(*top27, prefab_id);
  const auto specs = derive_attachment_specs(prefab_entry, decorations);
  return add_attachment_points(file, prefab_id, object_id, specs);
}

std::string attachment_from_decoration_summary_to_json(const AttachmentSummary& summary) {
  std::ostringstream out;
  out << "{"
      << "\"kind\":\"attachmentFromDecoration\","
      << "\"prefabId\":" << summary.prefab_id << ","
      << "\"objectId\":";
  if (summary.object_id) {
    out << *summary.object_id;
  } else {
    out << "null";
  }
  out << ",\"sceneInstanceCount\":" << summary.scene_instance_count << ","
      << "\"names\":" << string_array_json(summary.names) << ","
      << "\"changedTopFields\":[";
  for (size_t i = 0; i < summary.changed_top_fields.size(); ++i) {
    if (i) out << ",";
    out << summary.changed_top_fields[i];
  }
  out << "]}";
  return out.str();
}

}  // namespace opengil
