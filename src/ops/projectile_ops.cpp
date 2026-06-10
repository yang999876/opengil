#include "opengil/projectile_ops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "opengil/json.hpp"
#include "opengil/semantic.hpp"

namespace opengil {
namespace {

constexpr float PI = 3.14159265358979323846f;

template <size_t N>
std::optional<uint64_t> read_varint_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_varint_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

template <size_t N>
std::optional<std::string> read_string_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_string_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

template <size_t N>
std::optional<float> read_fixed32_path(std::span<const uint8_t> message, const std::array<uint32_t, N>& path) {
  return read_fixed32_at_path(message, std::span<const uint32_t>(path.data(), path.size()));
}

std::vector<uint8_t> fixed32_float_bytes(float value) {
  uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof(float));
  return {
      static_cast<uint8_t>(raw & 0xff),
      static_cast<uint8_t>((raw >> 8) & 0xff),
      static_cast<uint8_t>((raw >> 16) & 0xff),
      static_cast<uint8_t>((raw >> 24) & 0xff),
  };
}

OwnedField make_fixed32_field(uint32_t number, float value) {
  OwnedField field;
  field.number = number;
  field.wire = 5;
  field.data = fixed32_float_bytes(value);
  return field;
}

void set_fixed32_field_in_message(std::vector<OwnedField>& fields, uint32_t field_number, float value) {
  for (auto& field : fields) {
    if (field.number == field_number && field.wire == 5) {
      field.data = fixed32_float_bytes(value);
      return;
    }
  }
  fields.push_back(make_fixed32_field(field_number, value));
}

bool set_nested_fixed32_field(
    std::vector<OwnedField>& fields,
    std::span<const uint32_t> container_path,
    uint32_t field_number,
    float value) {
  if (container_path.empty()) return false;
  for (auto& field : fields) {
    if (field.number != container_path[0]) continue;
    if (container_path.size() == 1) {
      if (field.wire != 2) continue;
      auto child_fields = parse_owned_fields_or_throw(field.data, "projectile fixed32 child");
      set_fixed32_field_in_message(child_fields, field_number, value);
      field.data = rebuild_message(child_fields);
      return true;
    }
    if (field.wire != 2) continue;
    auto child_fields = parse_owned_fields_or_throw(field.data, "projectile fixed32 child");
    if (set_nested_fixed32_field(child_fields, container_path.subspan(1), field_number, value)) {
      field.data = rebuild_message(child_fields);
      return true;
    }
  }
  return false;
}

bool contains_projectile_display_name(std::string_view text) {
  const char bytes[] = {
      static_cast<char>(0xe6), static_cast<char>(0x8a), static_cast<char>(0x9b),
      static_cast<char>(0xe7), static_cast<char>(0x89), static_cast<char>(0xa9),
      static_cast<char>(0xe7), static_cast<char>(0xba), static_cast<char>(0xbf),
      static_cast<char>(0xe6), static_cast<char>(0x8a), static_cast<char>(0x95),
      static_cast<char>(0xe5), static_cast<char>(0xb0), static_cast<char>(0x84),
  };
  const std::string needle(bytes, sizeof(bytes));
  return text.find(needle) != std::string_view::npos;
}

bool has_projectile_motion_component(std::span<const uint8_t> component) {
  const std::array<uint32_t, 1> component_kind{1};
  if (read_varint_path(component, component_kind) != 11) return false;
  const std::array<uint32_t, 3> display_name_path{21, 1, 502};
  const auto name = read_string_path(component, display_name_path);
  return name && contains_projectile_display_name(*name);
}

std::string find_prefab_name(const GilFile& file, uint64_t prefab_id) {
  for (const auto& prefab : list_prefabs(file)) {
    if (prefab.prefab_id == prefab_id) return prefab.name;
  }
  return "";
}

std::vector<uint8_t> patch_projectile_entry(
    std::span<const uint8_t> entry,
    const ProjectileMotionInput& motion,
    ProjectileMotionSummary& summary) {
  auto fields = parse_owned_fields_or_throw(entry, "projectile prefab entry");
  bool changed = false;

  for (auto& field : fields) {
    if (changed || field.number != 8 || field.wire != 2) continue;
    const auto component = std::span<const uint8_t>(field.data.data(), field.data.size());
    if (!has_projectile_motion_component(component)) continue;

    const std::array<uint32_t, 5> x_path{21, 1, 12, 1, 1};
    const std::array<uint32_t, 5> y_path{21, 1, 12, 1, 2};
    const std::array<uint32_t, 4> gravity_path{21, 1, 12, 2};
    summary.before_x = read_fixed32_path(component, x_path);
    summary.before_y = read_fixed32_path(component, y_path);
    summary.before_gravity = read_fixed32_path(component, gravity_path);

    auto component_fields = parse_owned_fields_or_throw(component, "projectile component");
    const std::array<uint32_t, 4> velocity_container{21, 1, 12, 1};
    const std::array<uint32_t, 3> gravity_container{21, 1, 12};
    if (!set_nested_fixed32_field(component_fields, velocity_container, 1, motion.x)) {
      throw std::runtime_error("projectile velocity container path not found");
    }
    if (!set_nested_fixed32_field(component_fields, velocity_container, 2, motion.y)) {
      throw std::runtime_error("projectile velocity container path not found");
    }
    if (motion.gravity) {
      if (!set_nested_fixed32_field(component_fields, gravity_container, 2, *motion.gravity)) {
        throw std::runtime_error("projectile gravity container path not found");
      }
    }

    field.data = rebuild_message(component_fields);
    changed = true;
  }

  if (!changed) throw std::runtime_error("projectile motion component not found");
  return rebuild_message(fields);
}

std::vector<uint8_t> patch_top4_projectile(
    std::span<const uint8_t> top4,
    uint64_t prefab_id,
    const ProjectileMotionInput& motion,
    ProjectileMotionSummary& summary) {
  auto fields = parse_owned_fields_or_throw(top4, "projectile top4");
  bool found = false;

  for (auto& field : fields) {
    if (field.number != 1 || field.wire != 2) continue;
    const auto entry = std::span<const uint8_t>(field.data.data(), field.data.size());
    const std::array<uint32_t, 1> id_path{1};
    if (read_varint_path(entry, id_path) != prefab_id) continue;
    field.data = patch_projectile_entry(entry, motion, summary);
    found = true;
    break;
  }

  if (!found) throw std::runtime_error("prefab id not found");
  return rebuild_message(fields);
}

std::string optional_float_json(const std::optional<float>& value) {
  if (!value) return "null";
  std::ostringstream out;
  out << std::setprecision(9) << *value;
  return out.str();
}

std::string float_json(float value) {
  std::ostringstream out;
  out << std::setprecision(9) << value;
  return out.str();
}

}  // namespace

ProjectileMotionInput projectile_motion_from_angle(float angle_deg, float speed, std::optional<float> gravity) {
  const float radians = angle_deg * PI / 180.0f;
  ProjectileMotionInput input;
  input.x = std::cos(radians) * speed;
  input.y = std::sin(radians) * speed;
  input.gravity = gravity;
  return input;
}

ProjectileMutation set_prefab_projectile_motion(
    const GilFile& file,
    uint64_t prefab_id,
    const ProjectileMotionInput& motion) {
  const auto top4 = top_level_data(file, 4);
  if (!top4) throw std::runtime_error("top-level field 4 not found");

  ProjectileMotionSummary summary;
  summary.prefab_id = prefab_id;
  summary.prefab_name = find_prefab_name(file, prefab_id);
  summary.after_x = motion.x;
  summary.after_y = motion.y;

  auto next_top4 = patch_top4_projectile(*top4, prefab_id, motion, summary);
  std::vector<uint8_t> next_payload = replace_top_level_field_data(payload(file), 4, next_top4);
  summary.changed_top_fields.push_back(4);
  summary.after_gravity = motion.gravity ? motion.gravity : summary.before_gravity;

  ProjectileMutation mutation;
  mutation.payload = std::move(next_payload);
  mutation.bytes = build_gil_bytes(file.header, mutation.payload);
  mutation.summary = std::move(summary);
  return mutation;
}

std::string projectile_motion_summary_to_json(const ProjectileMotionSummary& summary) {
  std::ostringstream out;
  out << "{"
      << "\"prefabId\":" << summary.prefab_id << ","
      << "\"prefabName\":" << json::quote(summary.prefab_name) << ","
      << "\"before\":{"
      << "\"x\":" << optional_float_json(summary.before_x) << ","
      << "\"y\":" << optional_float_json(summary.before_y) << ","
      << "\"gravity\":" << optional_float_json(summary.before_gravity)
      << "},\"after\":{"
      << "\"x\":" << float_json(summary.after_x) << ","
      << "\"y\":" << float_json(summary.after_y) << ","
      << "\"gravity\":" << optional_float_json(summary.after_gravity)
      << "},\"changedTopFields\":" << json::array_of_numbers(summary.changed_top_fields)
      << "}";
  return out.str();
}

}  // namespace opengil
