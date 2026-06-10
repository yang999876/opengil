#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "opengil/attachment_from_decoration_ops.hpp"
#include "opengil/attachment_ops.hpp"
#include "opengil/gil.hpp"
#include "opengil/custom_vars_ops.hpp"
#include "opengil/decoration_ops.hpp"
#include "opengil/json.hpp"
#include "opengil/json_value.hpp"
#include "opengil/model_ops.hpp"
#include "opengil/nodegraph_ops.hpp"
#include "opengil/object_ops.hpp"
#include "opengil/prefab_ops.hpp"
#include "opengil/projectile_ops.hpp"
#include "opengil/semantic.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/version.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using opengil::GilFile;

constexpr int EXIT_OK = 0;
constexpr int EXIT_USAGE = 1;
constexpr int EXIT_PARSE = 2;
constexpr int EXIT_SEMANTIC = 3;
constexpr int EXIT_VALIDATE = 4;
constexpr int EXIT_WRITE = 5;

struct Args {
  std::string command;
  std::map<std::string, std::string> values;
  std::set<std::string> flags;
  std::vector<std::string> positional;
};

struct CliError : std::runtime_error {
  std::string code;
  int exit_code;

  CliError(std::string c, std::string message, int e)
      : std::runtime_error(std::move(message)), code(std::move(c)), exit_code(e) {}
};

struct BatchOp {
  std::string op;
  uint64_t prefab_id = 0;
  uint64_t object_id = 0;
  uint64_t source_prefab_id = 0;
  uint64_t target_prefab_id = 0;
  std::optional<uint64_t> asset_id;
  std::optional<uint64_t> nodegraph_id;
  std::optional<uint64_t> tab_id;
  std::optional<uint64_t> new_prefab_id;
  std::optional<uint64_t> requested_object_id;
  std::optional<uint64_t> prefab_id_start_after;
  std::optional<double> x;
  std::optional<double> y;
  std::optional<double> angle_deg;
  std::optional<double> speed;
  std::optional<double> gravity;
  std::optional<double> preview_x_step;
  std::optional<double> preview_z_step;
  std::optional<double> pos_x;
  std::optional<double> pos_y;
  std::optional<double> pos_z;
  std::optional<double> rot_x;
  std::optional<double> rot_y;
  std::optional<double> rot_z;
  std::optional<double> scale_x;
  std::optional<double> scale_y;
  std::optional<double> scale_z;
  std::string name;
  std::string display_name;
  std::string type;
  std::string tab;
  std::string template_path;
};

Args parse_args(int argc, char** argv) {
  Args args;
  if (argc <= 1) {
    throw CliError("USAGE", "missing command", EXIT_USAGE);
  }

  args.command = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string item = argv[i];
    if (item.rfind("--", 0) != 0) {
      args.positional.push_back(item);
      continue;
    }

    const std::string key = item.substr(2);
    if (key == "dry-run" || key == "in-place" || key == "human" || key == "summary") {
      args.flags.insert(key);
      continue;
    }

    if (i + 1 >= argc) {
      throw CliError("USAGE", "missing value for --" + key, EXIT_USAGE);
    }
    args.values[key] = argv[++i];
  }
  return args;
}

std::string value_or_empty(const Args& args, const std::string& key) {
  const auto it = args.values.find(key);
  return it == args.values.end() ? "" : it->second;
}

std::string require_value(const Args& args, const std::string& key) {
  const auto value = value_or_empty(args, key);
  if (value.empty()) {
    throw CliError("USAGE", "missing required --" + key, EXIT_USAGE);
  }
  return value;
}

uint64_t require_u64(const Args& args, const std::string& key) {
  const auto text = require_value(args, key);
  size_t consumed = 0;
  uint64_t value = 0;
  try {
    value = std::stoull(text, &consumed, 10);
  } catch (...) {
    throw CliError("USAGE", "--" + key + " must be an unsigned integer", EXIT_USAGE);
  }
  if (consumed != text.size()) {
    throw CliError("USAGE", "--" + key + " must be an unsigned integer", EXIT_USAGE);
  }
  return value;
}

std::optional<uint64_t> optional_u64(const Args& args, const std::string& key) {
  if (value_or_empty(args, key).empty()) return std::nullopt;
  return require_u64(args, key);
}

double require_double(const Args& args, const std::string& key) {
  const auto text = require_value(args, key);
  size_t consumed = 0;
  double value = 0.0;
  try {
    value = std::stod(text, &consumed);
  } catch (...) {
    throw CliError("USAGE", "--" + key + " must be a finite number", EXIT_USAGE);
  }
  if (consumed != text.size() || !std::isfinite(value)) {
    throw CliError("USAGE", "--" + key + " must be a finite number", EXIT_USAGE);
  }
  return value;
}

std::optional<double> optional_double(const Args& args, const std::string& key) {
  if (value_or_empty(args, key).empty()) return std::nullopt;
  return require_double(args, key);
}

std::string error_json(const std::string& code, const std::string& message) {
  return "{\"code\":" + opengil::json::quote(code) +
         ",\"message\":" + opengil::json::quote(message) +
         ",\"details\":{}}";
}

std::string file_input_json(const GilFile& file) {
  return "{\"path\":" + opengil::json::quote(file.path.string()) +
         ",\"sha256\":" + opengil::json::quote(opengil::file_sha256(file)) +
         "}";
}

std::string null_input_json() {
  return "null";
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw CliError("FILE_ERROR", "failed to open file: " + path.string(), EXIT_PARSE);
  }
  std::ostringstream out;
  out << stream.rdbuf();
  if (!stream.good() && !stream.eof()) {
    throw CliError("FILE_ERROR", "failed to read file: " + path.string(), EXIT_PARSE);
  }
  return out.str();
}

std::filesystem::path unique_temp_path_for(const std::filesystem::path& path) {
  const auto parent = path.parent_path();
  const auto stem = path.filename().string();
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  for (int attempt = 0; attempt < 100; ++attempt) {
    auto candidate = parent / (stem + ".tmp." + std::to_string(tick) + "." + std::to_string(attempt));
    if (!std::filesystem::exists(candidate)) return candidate;
  }
  throw CliError("WRITE_FAILED", "failed to allocate temporary output path for: " + path.string(), EXIT_WRITE);
}

std::filesystem::path backup_path_for(const std::filesystem::path& path) {
  auto backup = path;
  backup += ".bak";
  return backup;
}

void replace_file_atomic(const std::filesystem::path& temp_path, const std::filesystem::path& output_path) {
#ifdef _WIN32
  if (!MoveFileExW(
          temp_path.wstring().c_str(),
          output_path.wstring().c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw CliError("WRITE_FAILED", "failed to replace output file atomically: " + output_path.string(), EXIT_WRITE);
  }
#else
  std::error_code error;
  std::filesystem::rename(temp_path, output_path, error);
  if (error) {
    throw CliError("WRITE_FAILED", "failed to replace output file atomically: " + error.message(), EXIT_WRITE);
  }
#endif
}

void write_bytes_to_path(const std::filesystem::path& path, std::span<const uint8_t> bytes, bool keep_backup) {
  const auto parent = path.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);
  const auto temp_path = unique_temp_path_for(path);
  std::ofstream stream(temp_path, std::ios::binary | std::ios::trunc);
  if (!stream) throw CliError("WRITE_FAILED", "failed to open temporary output file: " + temp_path.string(), EXIT_WRITE);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.flush();
  if (!stream) {
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    throw CliError("WRITE_FAILED", "failed to write temporary output file: " + temp_path.string(), EXIT_WRITE);
  }
  stream.close();

  try {
    auto check_file = opengil::load_gil_file(temp_path);
    const auto validation = opengil::validate_gil(check_file);
    if (!validation.ok) {
      throw CliError("VALIDATION_FAILED", "temporary output failed validation", EXIT_VALIDATE);
    }

    if (keep_backup && std::filesystem::exists(path)) {
      std::error_code backup_error;
      std::filesystem::copy_file(
          path,
          backup_path_for(path),
          std::filesystem::copy_options::overwrite_existing,
          backup_error);
      if (backup_error) {
        throw CliError("WRITE_FAILED", "failed to create backup file: " + backup_error.message(), EXIT_WRITE);
      }
    }
    replace_file_atomic(temp_path, path);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    throw;
  }
}

void write_output_bytes(const Args& args, const std::filesystem::path& path, std::span<const uint8_t> bytes) {
  write_bytes_to_path(path, bytes, args.flags.contains("in-place"));
}

std::string output_file_json(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
  return "{\"path\":" + opengil::json::quote(path.string()) +
         ",\"sha256\":" + opengil::json::quote(opengil::bytes_sha256(bytes)) +
         "}";
}

std::string envelope(
    const std::string& command,
    bool ok,
    const std::string& input_json,
    const std::string& output_json,
    const std::string& result_json,
    const std::vector<std::string>& warnings,
    const std::vector<std::string>& errors) {
  std::ostringstream out;
  out << "{"
      << "\"schemaVersion\":1,"
      << "\"toolVersion\":" << opengil::json::quote(OPENGIL_VERSION) << ","
      << "\"ok\":" << opengil::json::bool_value(ok) << ","
      << "\"command\":" << opengil::json::quote(command) << ","
      << "\"input\":" << input_json << ","
      << "\"output\":" << output_json << ","
      << "\"result\":" << result_json << ","
      << "\"warnings\":[";
  for (size_t i = 0; i < warnings.size(); ++i) {
    if (i) out << ",";
    out << opengil::json::quote(warnings[i]);
  }
  out << "],\"errors\":[";
  for (size_t i = 0; i < errors.size(); ++i) {
    if (i) out << ",";
    out << errors[i];
  }
  out << "]}";
  return out.str();
}

void write_report_if_requested(const Args& args, const std::string& json) {
  const auto path = value_or_empty(args, "report");
  if (path.empty()) return;
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw CliError("WRITE_FAILED", "failed to open report path: " + path, EXIT_WRITE);
  }
  out << json << "\n";
}

std::string top_fields_json(const GilFile& file) {
  const auto fields = opengil::top_level_fields(file);
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) out << ",";
    out << "{"
        << "\"field\":" << fields[i].number << ","
        << "\"wire\":" << static_cast<int>(fields[i].wire) << ","
        << "\"rawStart\":" << fields[i].raw_start << ","
        << "\"rawEnd\":" << fields[i].raw_end << ","
        << "\"len\":" << (fields[i].raw_end - fields[i].raw_start)
        << "}";
  }
  out << "]";
  return out.str();
}

std::string inspect_result(const GilFile& file) {
  std::ostringstream out;
  out << "{"
      << "\"header\":{"
      << "\"leftSize\":" << file.header.left_size << ","
      << "\"schema\":" << file.header.schema << ","
      << "\"headTag\":" << file.header.head_tag << ","
      << "\"fileType\":" << file.header.file_type << ","
      << "\"protoSize\":" << file.header.proto_size << ","
      << "\"tailTag\":" << file.header.tail_tag
      << "},"
      << "\"fileSize\":" << file.bytes.size() << ","
      << "\"payloadSize\":" << opengil::payload(file).size() << ","
      << "\"topLevelFields\":" << top_fields_json(file)
      << "}";
  return out.str();
}

std::string validate_result_json(const opengil::ValidationResult& validation) {
  std::ostringstream out;
  out << "{"
      << "\"valid\":" << opengil::json::bool_value(validation.ok) << ","
      << "\"errors\":" << opengil::json::string_array(validation.errors) << ","
      << "\"warnings\":" << opengil::json::string_array(validation.warnings)
      << "}";
  return out.str();
}

std::string diff_summary_json(const GilFile& before, const GilFile& after) {
  const auto before_fields = opengil::top_level_fields(before);
  const auto after_fields = opengil::top_level_fields(after);
  std::map<uint32_t, std::string> before_hash;
  std::map<uint32_t, std::string> after_hash;

  const auto before_payload = opengil::payload(before);
  const auto after_payload = opengil::payload(after);
  for (const auto& field : before_fields) {
    before_hash[field.number] = opengil::bytes_sha256(before_payload.subspan(field.raw_start, field.raw_end - field.raw_start));
  }
  for (const auto& field : after_fields) {
    after_hash[field.number] = opengil::bytes_sha256(after_payload.subspan(field.raw_start, field.raw_end - field.raw_start));
  }

  std::set<uint32_t> all_fields;
  for (const auto& [field, _] : before_hash) all_fields.insert(field);
  for (const auto& [field, _] : after_hash) all_fields.insert(field);

  std::ostringstream changed;
  bool first = true;
  size_t count = 0;
  for (uint32_t field : all_fields) {
    if (before_hash[field] == after_hash[field]) continue;
    if (!first) changed << ",";
    first = false;
    count++;
    changed << field;
  }

  std::ostringstream out;
  out << "{"
      << "\"beforeSha256\":" << opengil::json::quote(opengil::file_sha256(before)) << ","
      << "\"afterSha256\":" << opengil::json::quote(opengil::file_sha256(after)) << ","
      << "\"beforePayloadSize\":" << opengil::payload(before).size() << ","
      << "\"afterPayloadSize\":" << opengil::payload(after).size() << ","
      << "\"changedTopFieldCount\":" << count << ","
      << "\"changedTopFields\":[" << changed.str() << "]"
      << "}";
  return out.str();
}

std::filesystem::path resolve_write_output_path(const Args& args, const std::filesystem::path& input_path) {
  if (args.flags.contains("in-place")) return input_path;
  const auto output = value_or_empty(args, "output");
  if (output.empty() && !args.flags.contains("dry-run")) {
    throw CliError("USAGE", "write commands require --output unless --dry-run or --in-place is used", EXIT_USAGE);
  }
  return output.empty() ? std::filesystem::path{} : std::filesystem::path(output);
}

const opengil::json::Value* find_any(
    const opengil::json::Value& object,
    std::initializer_list<std::string_view> keys) {
  for (std::string_view key : keys) {
    if (const auto* value = object.find(key)) return value;
  }
  return nullptr;
}

std::string batch_context(size_t index) {
  std::ostringstream out;
  out << "batch op " << index;
  return out.str();
}

std::string require_json_string(
    const opengil::json::Value& object,
    std::initializer_list<std::string_view> keys,
    size_t index) {
  const auto* value = find_any(object, keys);
  if (!value || !value->is_string()) {
    throw CliError("USAGE", batch_context(index) + " is missing a required string field", EXIT_USAGE);
  }
  return value->string_value;
}

uint64_t require_json_u64(
    const opengil::json::Value& object,
    std::initializer_list<std::string_view> keys,
    size_t index) {
  const auto* value = find_any(object, keys);
  if (!value || !value->is_unsigned()) {
    throw CliError("USAGE", batch_context(index) + " is missing a required unsigned integer field", EXIT_USAGE);
  }
  return value->unsigned_value;
}

std::optional<uint64_t> optional_json_u64(
    const opengil::json::Value& object,
    std::initializer_list<std::string_view> keys) {
  const auto* value = find_any(object, keys);
  if (!value) return std::nullopt;
  if (!value->is_unsigned()) {
    throw CliError("USAGE", "batch unsigned integer field must be a JSON integer >= 0", EXIT_USAGE);
  }
  return value->unsigned_value;
}

std::optional<std::string> optional_json_string(
    const opengil::json::Value& object,
    std::initializer_list<std::string_view> keys) {
  const auto* value = find_any(object, keys);
  if (!value) return std::nullopt;
  if (!value->is_string()) {
    throw CliError("USAGE", "batch string field must be a JSON string", EXIT_USAGE);
  }
  return value->string_value;
}

std::optional<double> optional_json_number(
    const opengil::json::Value& object,
    std::initializer_list<std::string_view> keys) {
  const auto* value = find_any(object, keys);
  if (!value) return std::nullopt;
  if (!value->is_number()) {
    throw CliError("USAGE", "batch numeric field must be a JSON number", EXIT_USAGE);
  }
  return value->number_value;
}

opengil::ProjectileMotionInput projectile_input_from_numbers(
    std::optional<double> x,
    std::optional<double> y,
    std::optional<double> angle_deg,
    std::optional<double> speed,
    std::optional<double> gravity) {
  std::optional<float> gravity_value;
  if (gravity) gravity_value = static_cast<float>(*gravity);
  if (angle_deg && speed) {
    return opengil::projectile_motion_from_angle(
        static_cast<float>(*angle_deg),
        static_cast<float>(*speed),
        gravity_value);
  }
  if (x && y) {
    opengil::ProjectileMotionInput input;
    input.x = static_cast<float>(*x);
    input.y = static_cast<float>(*y);
    input.gravity = gravity_value;
    return input;
  }
  throw CliError("USAGE", "projectile motion requires --x/--y or --angle/--speed", EXIT_USAGE);
}

opengil::ProjectileMotionInput projectile_input_from_args(const Args& args) {
  const auto angle_deg = optional_double(args, "angle-deg").has_value()
      ? optional_double(args, "angle-deg")
      : optional_double(args, "angle");
  return projectile_input_from_numbers(
      optional_double(args, "x"),
      optional_double(args, "y"),
      angle_deg,
      optional_double(args, "speed"),
      optional_double(args, "gravity"));
}

std::vector<BatchOp> parse_batch_ops_text(const std::string& text) {
  opengil::json::Value root;
  try {
    root = opengil::json::parse_value(text);
  } catch (const std::exception& error) {
    throw CliError("OPS_JSON_INVALID", error.what(), EXIT_USAGE);
  }

  const opengil::json::Value* ops = nullptr;
  if (root.is_array()) {
    ops = &root;
  } else if (root.is_object()) {
    ops = root.find("ops");
  }

  if (!ops || !ops->is_array()) {
    throw CliError("USAGE", "batch ops must be a JSON array or an object with an ops array", EXIT_USAGE);
  }

  std::vector<BatchOp> parsed;
  parsed.reserve(ops->array_value.size());
  for (size_t index = 0; index < ops->array_value.size(); ++index) {
    const auto& item = ops->array_value[index];
    if (!item.is_object()) {
      throw CliError("USAGE", batch_context(index) + " must be a JSON object", EXIT_USAGE);
    }

    BatchOp op;
    op.op = require_json_string(item, {"op", "command"}, index);
    const auto parse_transform_fields = [&]() {
      op.pos_x = optional_json_number(item, {"posX", "pos-x", "positionX", "position-x"});
      op.pos_y = optional_json_number(item, {"posY", "pos-y", "positionY", "position-y"});
      op.pos_z = optional_json_number(item, {"posZ", "pos-z", "positionZ", "position-z"});
      op.rot_x = optional_json_number(item, {"rotX", "rot-x", "rotationX", "rotation-x"});
      op.rot_y = optional_json_number(item, {"rotY", "rot-y", "rotationY", "rotation-y"});
      op.rot_z = optional_json_number(item, {"rotZ", "rot-z", "rotationZ", "rotation-z"});
      op.scale_x = optional_json_number(item, {"scaleX", "scale-x"});
      op.scale_y = optional_json_number(item, {"scaleY", "scale-y"});
      op.scale_z = optional_json_number(item, {"scaleZ", "scale-z"});
    };
    if (op.op == "set-model") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.asset_id = require_json_u64(item, {"assetId", "asset-id", "modelAssetId", "model-asset-id"}, index);
    } else if (op.op == "set-empty-model") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
    } else if (op.op == "rename-prefab") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.name = require_json_string(item, {"name", "newName", "new-name"}, index);
    } else if (op.op == "delete-prefab") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
    } else if (op.op == "attach-nodegraph") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.nodegraph_id = require_json_u64(item, {"nodegraphId", "nodegraph-id"}, index);
    } else if (op.op == "set-projectile-motion") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.x = optional_json_number(item, {"x", "velocityX", "velocity-x"});
      op.y = optional_json_number(item, {"y", "velocityY", "velocity-y"});
      op.angle_deg = optional_json_number(item, {"angleDeg", "angle-deg", "angle"});
      op.speed = optional_json_number(item, {"speed"});
      op.gravity = optional_json_number(item, {"gravity"});
      (void)projectile_input_from_numbers(op.x, op.y, op.angle_deg, op.speed, op.gravity);
    } else if (op.op == "custom-vars.add") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.name = require_json_string(item, {"name"}, index);
      op.type = require_json_string(item, {"type"}, index);
    } else if (op.op == "custom-vars.remove") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.name = require_json_string(item, {"name"}, index);
    } else if (op.op == "custom-vars.copy-all") {
      op.source_prefab_id = require_json_u64(item, {"sourcePrefabId", "source-prefab-id", "fromPrefabId", "from-prefab-id"}, index);
      op.target_prefab_id = require_json_u64(item, {"targetPrefabId", "target-prefab-id", "toPrefabId", "to-prefab-id"}, index);
    } else if (op.op == "custom-vars.sync-tab") {
      op.source_prefab_id = require_json_u64(item, {"sourcePrefabId", "source-prefab-id"}, index);
      op.tab_id = optional_json_u64(item, {"tabId", "tab-id"});
      op.tab = optional_json_string(item, {"tab", "tabName", "tab-name"}).value_or("");
      if (!op.tab_id && op.tab.empty()) {
        throw CliError("USAGE", batch_context(index) + " must include tabId or tab", EXIT_USAGE);
      }
    } else if (op.op == "clone-prefab") {
      op.source_prefab_id = require_json_u64(item, {"sourcePrefabId", "source-prefab-id"}, index);
      op.name = require_json_string(item, {"name", "newName", "new-name"}, index);
      op.tab_id = optional_json_u64(item, {"tabId", "tab-id"});
      op.tab = optional_json_string(item, {"tab", "tabName", "tab-name"}).value_or("");
      op.new_prefab_id = optional_json_u64(item, {"newPrefabId", "new-prefab-id", "prefabId", "prefab-id"});
      op.prefab_id_start_after = optional_json_u64(item, {"prefabIdStartAfter", "prefab-id-start-after"});
      op.preview_x_step = optional_json_number(item, {"previewXStep", "preview-x-step"});
      op.preview_z_step = optional_json_number(item, {"previewZStep", "preview-z-step"});
      if (!op.tab_id && op.tab.empty()) {
        throw CliError("USAGE", batch_context(index) + " must include tabId or tab", EXIT_USAGE);
      }
    } else if (op.op == "copy-prefab-to-tab") {
      op.source_prefab_id = require_json_u64(item, {"sourcePrefabId", "source-prefab-id"}, index);
      op.name = optional_json_string(item, {"name", "newName", "new-name"}).value_or("");
      op.tab_id = optional_json_u64(item, {"tabId", "tab-id"});
      op.tab = optional_json_string(item, {"tab", "tabName", "tab-name"}).value_or("");
      op.new_prefab_id = optional_json_u64(item, {"newPrefabId", "new-prefab-id", "prefabId", "prefab-id"});
      op.prefab_id_start_after = optional_json_u64(item, {"prefabIdStartAfter", "prefab-id-start-after"});
      op.preview_x_step = optional_json_number(item, {"previewXStep", "preview-x-step"});
      op.preview_z_step = optional_json_number(item, {"previewZStep", "preview-z-step"});
      if (!op.tab_id && op.tab.empty()) {
        throw CliError("USAGE", batch_context(index) + " must include tabId or tab", EXIT_USAGE);
      }
    } else if (op.op == "create-scene-object") {
      op.asset_id = require_json_u64(item, {"assetId", "asset-id"}, index);
      op.requested_object_id = optional_json_u64(item, {"objectId", "object-id"});
      parse_transform_fields();
    } else if (op.op == "create-prefab") {
      op.asset_id = require_json_u64(item, {"assetId", "asset-id"}, index);
      op.new_prefab_id = optional_json_u64(item, {"prefabId", "prefab-id", "newPrefabId", "new-prefab-id"});
      op.template_path = optional_json_string(item, {"template", "templatePath", "template-path"}).value_or("");
      parse_transform_fields();
    } else if (op.op == "create-scene-prefab-instance") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.asset_id = require_json_u64(item, {"assetId", "asset-id"}, index);
      op.requested_object_id = optional_json_u64(item, {"objectId", "object-id"});
      op.template_path = optional_json_string(item, {"template", "templatePath", "template-path"}).value_or("");
      parse_transform_fields();
    } else if (op.op == "decoration.add") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.asset_id = require_json_u64(item, {"assetId", "asset-id"}, index);
      op.name = require_json_string(item, {"name"}, index);
      parse_transform_fields();
    } else if (op.op == "attachment.add") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.requested_object_id = optional_json_u64(item, {"objectId", "object-id"});
      op.name = require_json_string(item, {"name"}, index);
      op.display_name = require_json_string(item, {"displayName", "display-name"}, index);
      parse_transform_fields();
    } else if (op.op == "set-scene-transform" || op.op == "set-preview-transform") {
      op.object_id = require_json_u64(item, {"objectId", "object-id"}, index);
      parse_transform_fields();
    } else {
      throw CliError("USAGE", batch_context(index) + " uses unsupported op: " + op.op, EXIT_USAGE);
    }
    parsed.push_back(std::move(op));
  }

  return parsed;
}

void add_changed_fields(std::set<uint32_t>& changed, const std::vector<uint32_t>& fields) {
  for (uint32_t field : fields) changed.insert(field);
}

opengil::DecorationSpec decoration_spec_from_args(const Args& args);
opengil::AttachmentPointSpec attachment_spec_from_args(const Args& args);

std::string handle_set_model(const Args& args, bool empty_model) {
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const uint64_t prefab_id = require_u64(args, "prefab-id");
  const uint64_t model_asset_id = empty_model ? opengil::EMPTY_MODEL_ASSET_ID : require_u64(args, "asset-id");
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");

  const auto mutation = opengil::set_prefab_model_asset_id(file, prefab_id, model_asset_id);
  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  std::string result = opengil::set_model_summary_to_json(mutation.model_summary);
  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(args.command, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_set_projectile_motion(const Args& args) {
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const uint64_t prefab_id = require_u64(args, "prefab-id");
  const auto motion = projectile_input_from_args(args);
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");

  const auto mutation = opengil::set_prefab_projectile_motion(file, prefab_id, motion);
  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  std::string result = opengil::projectile_motion_summary_to_json(mutation.summary);
  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(args.command, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_custom_vars(const Args& args) {
  if (args.positional.empty()) {
    throw CliError("USAGE", "custom-vars requires a subcommand: list, add, remove, copy-all, sync-tab", EXIT_USAGE);
  }

  const std::string subcommand = args.positional[0];
  const std::string command_name = "custom-vars." + subcommand;
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);

  if (subcommand == "list") {
    const auto rows = opengil::list_prefab_custom_variables(file, optional_u64(args, "prefab-id"));
    const auto result = opengil::custom_variables_list_to_json(rows);
    const auto json = envelope(command_name, true, file_input_json(file), "null", result, {}, {});
    write_report_if_requested(args, json);
    return json;
  }

  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");
  opengil::CustomVarsMutation mutation;

  if (subcommand == "add") {
    mutation = opengil::add_prefab_custom_variable(
        file,
        require_u64(args, "prefab-id"),
        require_value(args, "name"),
        require_value(args, "type"));
  } else if (subcommand == "remove") {
    mutation = opengil::remove_prefab_custom_variable(
        file,
        require_u64(args, "prefab-id"),
        require_value(args, "name"));
  } else if (subcommand == "copy-all") {
    mutation = opengil::copy_prefab_custom_variables(
        file,
        require_u64(args, "from-prefab-id"),
        require_u64(args, "to-prefab-id"));
  } else if (subcommand == "sync-tab") {
    const uint64_t source_prefab_id = require_u64(args, "source-prefab-id");
    if (const auto tab_id = optional_u64(args, "tab-id")) {
      mutation = opengil::sync_tab_custom_variables_by_tab_id(file, source_prefab_id, *tab_id);
    } else {
      mutation = opengil::sync_tab_custom_variables(file, source_prefab_id, require_value(args, "tab"));
    }
  } else {
    throw CliError("USAGE", "unsupported custom-vars subcommand: " + subcommand, EXIT_USAGE);
  }

  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  std::string result = mutation.result_json;
  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(command_name, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_decoration(const Args& args) {
  if (args.positional.empty()) {
    throw CliError("USAGE", "decoration requires a subcommand: add", EXIT_USAGE);
  }
  const std::string subcommand = args.positional[0];
  if (subcommand != "add") {
    throw CliError("USAGE", "unsupported decoration subcommand: " + subcommand, EXIT_USAGE);
  }

  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");
  const uint64_t prefab_id = require_u64(args, "prefab-id");
  const auto spec = decoration_spec_from_args(args);

  const auto mutation = opengil::add_prefab_decorations(file, prefab_id, {spec});
  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  std::string result = opengil::decoration_summary_to_json(mutation.summary);
  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope("decoration.add", true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_attachment(const Args& args) {
  if (args.positional.empty()) {
    throw CliError("USAGE", "attachment requires a subcommand: add or from-decoration", EXIT_USAGE);
  }
  const std::string subcommand = args.positional[0];
  if (subcommand != "add" && subcommand != "from-decoration") {
    throw CliError("USAGE", "unsupported attachment subcommand: " + subcommand, EXIT_USAGE);
  }

  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");
  const uint64_t prefab_id = require_u64(args, "prefab-id");
  const auto object_id = optional_u64(args, "object-id");

  opengil::AttachmentMutation mutation;
  std::string result;
  std::string command_name;
  if (subcommand == "add") {
    const auto spec = attachment_spec_from_args(args);
    mutation = opengil::add_attachment_points(file, prefab_id, object_id, {spec});
    result = opengil::attachment_summary_to_json(mutation.summary);
    command_name = "attachment.add";
  } else {
    mutation = opengil::add_attachment_points_from_decorations(file, prefab_id, object_id);
    result = opengil::attachment_from_decoration_summary_to_json(mutation.summary);
    command_name = "attachment.from-decoration";
  }

  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(command_name, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_ui(const Args& args) {
  if (args.positional.empty()) {
    throw CliError("USAGE", "ui requires a subcommand: list", EXIT_USAGE);
  }
  const std::string subcommand = args.positional[0];
  if (subcommand != "list") {
    throw CliError("USAGE", "unsupported ui subcommand: " + subcommand, EXIT_USAGE);
  }

  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const uint64_t controller_entry_id = optional_u64(args, "controller-entry-id")
      .value_or(opengil::kDefaultUiPrimitiveControllerEntryId);
  const auto list = opengil::list_ui_primitives(file, controller_entry_id);
  const auto result = opengil::ui_primitive_list_to_json(list);
  const auto json = envelope("ui.list", true, file_input_json(file), "null", result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_attach_nodegraph(const Args& args, bool attach_all) {
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const uint64_t prefab_id = require_u64(args, "prefab-id");
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");

  std::vector<uint8_t> bytes;
  std::string result;
  if (attach_all) {
    const auto mutation = opengil::attach_all_nodegraphs_to_prefab(file, prefab_id);
    bytes = mutation.bytes;
    result = opengil::attach_all_nodegraphs_summary_to_json(mutation.summary);
  } else {
    const uint64_t nodegraph_id = require_u64(args, "nodegraph-id");
    const auto mutation = opengil::attach_nodegraph_to_prefab(file, prefab_id, nodegraph_id);
    bytes = mutation.bytes;
    result = opengil::attach_nodegraph_summary_to_json(mutation.summary);
  }

  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, bytes);
    output_json = output_file_json(output_path, bytes);
  }

  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(args.command, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_rename_prefab(const Args& args) {
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const uint64_t prefab_id = require_u64(args, "prefab-id");
  const auto new_name = require_value(args, "name");
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");

  const auto mutation = opengil::rename_prefab(file, prefab_id, new_name);
  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  std::string result = opengil::rename_prefab_summary_to_json(mutation.summary);
  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(args.command, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_delete_prefab(const Args& args) {
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const uint64_t prefab_id = require_u64(args, "prefab-id");
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");

  const auto mutation = opengil::delete_prefab(file, prefab_id);
  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  std::string result = opengil::delete_prefab_summary_to_json(mutation.summary);
  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(args.command, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

opengil::ClonePrefabOptions clone_options_from_args(const Args& args) {
  opengil::ClonePrefabOptions options;
  options.new_prefab_id = optional_u64(args, "new-prefab-id");
  options.prefab_id_start_after = optional_u64(args, "prefab-id-start-after");
  if (const auto value = optional_double(args, "preview-x-step")) options.preview_x_step = *value;
  if (const auto value = optional_double(args, "preview-z-step")) options.preview_z_step = *value;
  return options;
}

opengil::ClonePrefabOptions clone_options_from_batch_op(const BatchOp& op) {
  opengil::ClonePrefabOptions options;
  options.new_prefab_id = op.new_prefab_id;
  options.prefab_id_start_after = op.prefab_id_start_after;
  if (op.preview_x_step) options.preview_x_step = *op.preview_x_step;
  if (op.preview_z_step) options.preview_z_step = *op.preview_z_step;
  return options;
}

opengil::Transform transform_from_values(
    std::optional<double> pos_x,
    std::optional<double> pos_y,
    std::optional<double> pos_z,
    std::optional<double> rot_x,
    std::optional<double> rot_y,
    std::optional<double> rot_z,
    std::optional<double> scale_x,
    std::optional<double> scale_y,
    std::optional<double> scale_z) {
  opengil::Transform transform;
  if (pos_x) transform.position.x = *pos_x;
  if (pos_y) transform.position.y = *pos_y;
  if (pos_z) transform.position.z = *pos_z;
  if (rot_x) transform.rotation.x = *rot_x;
  if (rot_y) transform.rotation.y = *rot_y;
  if (rot_z) transform.rotation.z = *rot_z;
  if (scale_x) transform.scale.x = *scale_x;
  if (scale_y) transform.scale.y = *scale_y;
  if (scale_z) transform.scale.z = *scale_z;
  return transform;
}

opengil::Transform transform_from_args(const Args& args) {
  return transform_from_values(
      optional_double(args, "pos-x"),
      optional_double(args, "pos-y"),
      optional_double(args, "pos-z"),
      optional_double(args, "rot-x"),
      optional_double(args, "rot-y"),
      optional_double(args, "rot-z"),
      optional_double(args, "scale-x"),
      optional_double(args, "scale-y"),
      optional_double(args, "scale-z"));
}

opengil::Transform transform_from_batch_op(const BatchOp& op) {
  return transform_from_values(
      op.pos_x,
      op.pos_y,
      op.pos_z,
      op.rot_x,
      op.rot_y,
      op.rot_z,
      op.scale_x,
      op.scale_y,
      op.scale_z);
}

opengil::DecorationSpec decoration_spec_from_args(const Args& args) {
  opengil::DecorationSpec spec;
  spec.asset_id = require_u64(args, "asset-id");
  spec.name = require_value(args, "name");
  spec.transform = transform_from_args(args);
  return spec;
}

opengil::DecorationSpec decoration_spec_from_batch_op(const BatchOp& op) {
  opengil::DecorationSpec spec;
  spec.asset_id = *op.asset_id;
  spec.name = op.name;
  spec.transform = transform_from_batch_op(op);
  return spec;
}

opengil::AttachmentPointSpec attachment_spec_from_values(
    const std::string& name,
    const std::string& display_name,
    const opengil::Transform& transform) {
  opengil::AttachmentPointSpec spec;
  spec.name = name;
  spec.display_name = display_name;
  spec.x = transform.position.x;
  spec.y = transform.position.y;
  spec.rot_x = transform.rotation.x;
  spec.rot_y = transform.rotation.y;
  return spec;
}

opengil::AttachmentPointSpec attachment_spec_from_args(const Args& args) {
  return attachment_spec_from_values(
      require_value(args, "name"),
      require_value(args, "display-name"),
      transform_from_args(args));
}

opengil::AttachmentPointSpec attachment_spec_from_batch_op(const BatchOp& op) {
  return attachment_spec_from_values(op.name, op.display_name, transform_from_batch_op(op));
}

std::string handle_create_object(const Args& args) {
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");
  const auto transform = transform_from_args(args);

  std::optional<GilFile> template_file;
  if (const auto template_path = value_or_empty(args, "template"); !template_path.empty()) {
    template_file = opengil::load_gil_file(template_path);
  }

  opengil::ObjectMutation mutation;
  try {
    if (args.command == "create-scene-object") {
      opengil::CreateSceneObjectOptions options;
      options.object_id = optional_u64(args, "object-id");
      options.transform = transform;
      mutation = opengil::create_scene_object(file, require_u64(args, "asset-id"), options);
    } else if (args.command == "create-prefab") {
      opengil::CreatePrefabOptions options;
      options.prefab_id = optional_u64(args, "prefab-id");
      options.transform = transform;
      mutation = opengil::create_prefab(file, require_u64(args, "asset-id"), options, template_file ? &*template_file : nullptr);
    } else if (args.command == "create-scene-prefab-instance") {
      opengil::CreateScenePrefabInstanceOptions options;
      options.object_id = optional_u64(args, "object-id");
      options.transform = transform;
      mutation = opengil::create_scene_prefab_instance(
          file,
          require_u64(args, "prefab-id"),
          require_u64(args, "asset-id"),
          options,
          template_file ? &*template_file : nullptr);
    } else {
      throw CliError("USAGE", "unsupported create command: " + args.command, EXIT_USAGE);
    }
  } catch (const CliError&) {
    throw;
  } catch (const std::exception& error) {
    throw CliError("SEMANTIC_ERROR", error.what(), EXIT_SEMANTIC);
  }

  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  std::string result = mutation.result_json;
  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(args.command, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_clone_prefab(const Args& args) {
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const uint64_t source_prefab_id = require_u64(args, "source-prefab-id");
  const bool copy_command = args.command == "copy-prefab-to-tab";
  auto new_name = value_or_empty(args, "new-name");
  if (new_name.empty()) new_name = value_or_empty(args, "name");
  if (!copy_command && new_name.empty()) {
    throw CliError("USAGE", "missing required --new-name or --name", EXIT_USAGE);
  }
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");
  const auto options = clone_options_from_args(args);

  opengil::PrefabCloneMutation mutation;
  const std::optional<std::string> optional_name = new_name.empty()
      ? std::nullopt
      : std::optional<std::string>(new_name);
  if (copy_command) {
    if (const auto tab_id = optional_u64(args, "tab-id")) {
      mutation = opengil::copy_prefab_to_tab_by_id(file, source_prefab_id, *tab_id, optional_name, options);
    } else {
      mutation = opengil::copy_prefab_to_tab(file, source_prefab_id, require_value(args, "tab"), optional_name, options);
    }
  } else if (const auto tab_id = optional_u64(args, "tab-id")) {
    mutation = opengil::clone_prefab_into_tab_by_id(file, source_prefab_id, *tab_id, new_name, options);
  } else {
    mutation = opengil::clone_prefab_into_tab(file, source_prefab_id, require_value(args, "tab"), new_name, options);
  }

  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  std::string result = copy_command
      ? opengil::copy_prefab_summary_to_json(mutation.summary)
      : opengil::clone_prefab_summary_to_json(mutation.summary);
  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(args.command, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_set_transform(const Args& args, bool preview_space) {
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const uint64_t object_id = require_u64(args, "object-id");
  const auto transform = transform_from_args(args);
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");

  const auto mutation = preview_space
      ? opengil::set_preview_transform(file, object_id, transform)
      : opengil::set_scene_transform(file, object_id, transform);

  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, mutation.bytes);
    output_json = output_file_json(output_path, mutation.bytes);
  }

  std::string result = mutation.result_json;
  if (dry_run) {
    result.pop_back();
    result += ",\"dryRun\":true}";
  }
  const auto json = envelope(args.command, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_batch(const Args& args) {
  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile input_file = opengil::load_gil_file(input_path);
  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");
  const auto ops_path = std::filesystem::path(require_value(args, "ops"));
  const auto ops = parse_batch_ops_text(read_text_file(ops_path));

  GilFile current = input_file;
  std::vector<uint8_t> final_bytes = input_file.bytes;
  std::vector<std::string> item_jsons;
  std::set<uint32_t> changed_top_fields;
  item_jsons.reserve(ops.size());

  for (size_t index = 0; index < ops.size(); ++index) {
    const auto& op = ops[index];
    try {
      std::string result_json;
      if (op.op == "set-model") {
        const auto mutation = opengil::set_prefab_model_asset_id(current, op.prefab_id, *op.asset_id);
        result_json = opengil::set_model_summary_to_json(mutation.model_summary);
        add_changed_fields(changed_top_fields, mutation.model_summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "set-empty-model") {
        const auto mutation = opengil::set_prefab_to_empty_model(current, op.prefab_id);
        result_json = opengil::set_model_summary_to_json(mutation.model_summary);
        add_changed_fields(changed_top_fields, mutation.model_summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "rename-prefab") {
        const auto mutation = opengil::rename_prefab(current, op.prefab_id, op.name);
        result_json = opengil::rename_prefab_summary_to_json(mutation.summary);
        add_changed_fields(changed_top_fields, mutation.summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "delete-prefab") {
        const auto mutation = opengil::delete_prefab(current, op.prefab_id);
        result_json = opengil::delete_prefab_summary_to_json(mutation.summary);
        add_changed_fields(changed_top_fields, mutation.summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "attach-nodegraph") {
        const auto mutation = opengil::attach_nodegraph_to_prefab(current, op.prefab_id, *op.nodegraph_id);
        result_json = opengil::attach_nodegraph_summary_to_json(mutation.summary);
        add_changed_fields(changed_top_fields, mutation.summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "set-projectile-motion") {
        const auto motion = projectile_input_from_numbers(op.x, op.y, op.angle_deg, op.speed, op.gravity);
        const auto mutation = opengil::set_prefab_projectile_motion(current, op.prefab_id, motion);
        result_json = opengil::projectile_motion_summary_to_json(mutation.summary);
        add_changed_fields(changed_top_fields, mutation.summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "custom-vars.add") {
        const auto mutation = opengil::add_prefab_custom_variable(current, op.prefab_id, op.name, op.type);
        result_json = mutation.result_json;
        add_changed_fields(changed_top_fields, mutation.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "custom-vars.remove") {
        const auto mutation = opengil::remove_prefab_custom_variable(current, op.prefab_id, op.name);
        result_json = mutation.result_json;
        add_changed_fields(changed_top_fields, mutation.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "custom-vars.copy-all") {
        const auto mutation = opengil::copy_prefab_custom_variables(current, op.source_prefab_id, op.target_prefab_id);
        result_json = mutation.result_json;
        add_changed_fields(changed_top_fields, mutation.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "custom-vars.sync-tab") {
        const auto mutation = op.tab_id
            ? opengil::sync_tab_custom_variables_by_tab_id(current, op.source_prefab_id, *op.tab_id)
            : opengil::sync_tab_custom_variables(current, op.source_prefab_id, op.tab);
        result_json = mutation.result_json;
        add_changed_fields(changed_top_fields, mutation.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "clone-prefab") {
        const auto options = clone_options_from_batch_op(op);
        const auto mutation = op.tab_id
            ? opengil::clone_prefab_into_tab_by_id(current, op.source_prefab_id, *op.tab_id, op.name, options)
            : opengil::clone_prefab_into_tab(current, op.source_prefab_id, op.tab, op.name, options);
        result_json = opengil::clone_prefab_summary_to_json(mutation.summary);
        add_changed_fields(changed_top_fields, mutation.summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "copy-prefab-to-tab") {
        const auto options = clone_options_from_batch_op(op);
        const std::optional<std::string> optional_name = op.name.empty()
            ? std::nullopt
            : std::optional<std::string>(op.name);
        const auto mutation = op.tab_id
            ? opengil::copy_prefab_to_tab_by_id(current, op.source_prefab_id, *op.tab_id, optional_name, options)
            : opengil::copy_prefab_to_tab(current, op.source_prefab_id, op.tab, optional_name, options);
        result_json = opengil::copy_prefab_summary_to_json(mutation.summary);
        add_changed_fields(changed_top_fields, mutation.summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "create-scene-object") {
        opengil::CreateSceneObjectOptions options;
        options.object_id = op.requested_object_id;
        options.transform = transform_from_batch_op(op);
        const auto mutation = opengil::create_scene_object(current, *op.asset_id, options);
        result_json = mutation.result_json;
        add_changed_fields(changed_top_fields, mutation.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "create-prefab") {
        std::optional<GilFile> template_file;
        if (!op.template_path.empty()) template_file = opengil::load_gil_file(op.template_path);
        opengil::CreatePrefabOptions options;
        options.prefab_id = op.new_prefab_id;
        options.transform = transform_from_batch_op(op);
        const auto mutation = opengil::create_prefab(
            current,
            *op.asset_id,
            options,
            template_file ? &*template_file : nullptr);
        result_json = mutation.result_json;
        add_changed_fields(changed_top_fields, mutation.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "create-scene-prefab-instance") {
        std::optional<GilFile> template_file;
        if (!op.template_path.empty()) template_file = opengil::load_gil_file(op.template_path);
        opengil::CreateScenePrefabInstanceOptions options;
        options.object_id = op.requested_object_id;
        options.transform = transform_from_batch_op(op);
        const auto mutation = opengil::create_scene_prefab_instance(
            current,
            op.prefab_id,
            *op.asset_id,
            options,
            template_file ? &*template_file : nullptr);
        result_json = mutation.result_json;
        add_changed_fields(changed_top_fields, mutation.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "decoration.add") {
        const auto spec = decoration_spec_from_batch_op(op);
        const auto mutation = opengil::add_prefab_decorations(current, op.prefab_id, {spec});
        result_json = opengil::decoration_summary_to_json(mutation.summary);
        add_changed_fields(changed_top_fields, mutation.summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "attachment.add") {
        const auto spec = attachment_spec_from_batch_op(op);
        const auto mutation = opengil::add_attachment_points(current, op.prefab_id, op.requested_object_id, {spec});
        result_json = opengil::attachment_summary_to_json(mutation.summary);
        add_changed_fields(changed_top_fields, mutation.summary.changed_top_fields);
        final_bytes = mutation.bytes;
      } else if (op.op == "set-scene-transform" || op.op == "set-preview-transform") {
        const auto transform = transform_from_batch_op(op);
        const auto mutation = op.op == "set-preview-transform"
            ? opengil::set_preview_transform(current, op.object_id, transform)
            : opengil::set_scene_transform(current, op.object_id, transform);
        result_json = mutation.result_json;
        add_changed_fields(changed_top_fields, mutation.changed_top_fields);
        final_bytes = mutation.bytes;
      }

      current.bytes = final_bytes;
      std::ostringstream item;
      item << "{"
           << "\"index\":" << index << ","
           << "\"op\":" << opengil::json::quote(op.op) << ","
           << "\"result\":" << result_json
           << "}";
      item_jsons.push_back(item.str());
    } catch (const std::exception& error) {
      throw CliError(
          "BATCH_OP_FAILED",
          batch_context(index) + " (" + op.op + ") failed: " + error.what(),
          EXIT_SEMANTIC);
    }
  }

  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, final_bytes);
    output_json = output_file_json(output_path, final_bytes);
  }

  std::ostringstream result;
  result << "{"
         << "\"opCount\":" << ops.size() << ","
         << "\"changedTopFields\":[";
  bool first = true;
  for (uint32_t field : changed_top_fields) {
    if (!first) result << ",";
    first = false;
    result << field;
  }
  result << "],\"items\":[";
  for (size_t i = 0; i < item_jsons.size(); ++i) {
    if (i) result << ",";
    result << item_jsons[i];
  }
  result << "]";
  if (dry_run) result << ",\"dryRun\":true";
  result << "}";

  const auto json = envelope(args.command, true, file_input_json(input_file), output_json, result.str(), {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_with_input(const Args& args) {
  const auto input_path = require_value(args, "input");
  GilFile file = opengil::load_gil_file(input_path);

  std::string result;
  int validate_exit = EXIT_OK;
  if (args.command == "inspect") {
    result = inspect_result(file);
  } else if (args.command == "validate") {
    const auto validation = opengil::validate_gil(file);
    result = validate_result_json(validation);
    validate_exit = validation.ok ? EXIT_OK : EXIT_VALIDATE;
  } else if (args.command == "list-tabs") {
    result = opengil::tabs_to_json(opengil::list_tabs(file));
  } else if (args.command == "list-prefabs") {
    const auto tab = value_or_empty(args, "tab");
    result = opengil::prefabs_to_json(opengil::list_prefabs(file, tab.empty() ? std::nullopt : std::optional<std::string>(tab)));
  } else if (args.command == "list-prefab-tabs") {
    const uint64_t prefab_id = require_u64(args, "prefab-id");
    result = opengil::prefab_tabs_to_json(opengil::list_prefab_tabs(file, prefab_id));
  } else if (args.command == "get-model") {
    const uint64_t prefab_id = require_u64(args, "prefab-id");
    const auto model = opengil::get_model_info(file, prefab_id);
    if (!model) {
      throw CliError("PREFAB_NOT_FOUND", "prefab id not found", EXIT_SEMANTIC);
    }
    result = opengil::model_info_to_json(*model);
  } else if (args.command == "list-nodegraphs") {
    result = opengil::nodegraphs_to_json(opengil::list_nodegraphs(file));
  } else {
    throw CliError(
        "NOT_IMPLEMENTED",
        "this command is planned but not implemented in the current openGil milestone",
        EXIT_USAGE);
  }

  const auto json = envelope(args.command, validate_exit == EXIT_OK, file_input_json(file), "null", result, {}, {});
  write_report_if_requested(args, json);
  if (validate_exit != EXIT_OK) {
    std::cout << json << "\n";
    std::exit(validate_exit);
  }
  return json;
}

std::string handle_diff_summary(const Args& args) {
  GilFile before = opengil::load_gil_file(require_value(args, "before"));
  GilFile after = opengil::load_gil_file(require_value(args, "after"));
  const auto result = diff_summary_json(before, after);
  const auto input = "{\"before\":" + file_input_json(before) + ",\"after\":" + file_input_json(after) + "}";
  const auto json = envelope(args.command, true, input, "null", result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_version() {
  return envelope(
      "version",
      true,
      null_input_json(),
      "null",
      "{\"version\":" + opengil::json::quote(OPENGIL_VERSION) + "}",
      {},
      {});
}

}  // namespace

int main(int argc, char** argv) {
  try {
    Args args = parse_args(argc, argv);
    if (args.command == "--version" || args.command == "version") {
      std::cout << handle_version() << "\n";
      return EXIT_OK;
    }

    std::string output;
    if (args.command == "diff-summary") {
      output = handle_diff_summary(args);
    } else if (args.command == "set-model") {
      output = handle_set_model(args, false);
    } else if (args.command == "set-empty-model") {
      output = handle_set_model(args, true);
    } else if (args.command == "rename-prefab") {
      output = handle_rename_prefab(args);
    } else if (args.command == "delete-prefab") {
      output = handle_delete_prefab(args);
    } else if (args.command == "clone-prefab") {
      output = handle_clone_prefab(args);
    } else if (args.command == "copy-prefab-to-tab") {
      output = handle_clone_prefab(args);
    } else if (args.command == "create-scene-object" ||
               args.command == "create-prefab" ||
               args.command == "create-scene-prefab-instance") {
      output = handle_create_object(args);
    } else if (args.command == "set-scene-transform") {
      output = handle_set_transform(args, false);
    } else if (args.command == "set-preview-transform") {
      output = handle_set_transform(args, true);
    } else if (args.command == "attach-nodegraph") {
      output = handle_attach_nodegraph(args, false);
    } else if (args.command == "attach-all-nodegraphs") {
      output = handle_attach_nodegraph(args, true);
    } else if (args.command == "set-projectile-motion") {
      output = handle_set_projectile_motion(args);
    } else if (args.command == "custom-vars") {
      output = handle_custom_vars(args);
    } else if (args.command == "decoration") {
      output = handle_decoration(args);
    } else if (args.command == "attachment") {
      output = handle_attachment(args);
    } else if (args.command == "ui") {
      output = handle_ui(args);
    } else if (args.command == "batch") {
      output = handle_batch(args);
    } else {
      output = handle_with_input(args);
    }
    std::cout << output << "\n";
    return EXIT_OK;
  } catch (const CliError& error) {
    const auto json = envelope(
        "error",
        false,
        null_input_json(),
        "null",
        "{}",
        {},
        {error_json(error.code, error.what())});
    std::cout << json << "\n";
    return error.exit_code;
  } catch (const std::exception& error) {
    const auto json = envelope(
        "error",
        false,
        null_input_json(),
        "null",
        "{}",
        {},
        {error_json("UNHANDLED_ERROR", error.what())});
    std::cout << json << "\n";
    return EXIT_PARSE;
  }
}
