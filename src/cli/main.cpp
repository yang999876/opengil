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
#include "opengil/model_ops.hpp"
#include "opengil/nodegraph_ops.hpp"
#include "opengil/object_ops.hpp"
#include "opengil/prefab_ops.hpp"
#include "opengil/projectile_ops.hpp"
#include "opengil/semantic.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/ui_patch_ops.hpp"
#include "opengil/ui_pixel_import_ops.hpp"
#include "opengil/ui_structure_ops.hpp"
#include "opengil/version.hpp"

#include "json_formatters.hpp"

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

opengil::UiPrimitiveTransform ui_transform_from_args(const Args& args);

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

int64_t require_i64(const Args& args, const std::string& key) {
  const auto text = require_value(args, key);
  size_t consumed = 0;
  int64_t value = 0;
  try {
    value = std::stoll(text, &consumed, 10);
  } catch (...) {
    throw CliError("USAGE", "--" + key + " must be an integer", EXIT_USAGE);
  }
  if (consumed != text.size()) {
    throw CliError("USAGE", "--" + key + " must be an integer", EXIT_USAGE);
  }
  return value;
}

std::optional<uint64_t> optional_u64(const Args& args, const std::string& key) {
  if (value_or_empty(args, key).empty()) return std::nullopt;
  return require_u64(args, key);
}

size_t require_size(const Args& args, const std::string& key) {
  return static_cast<size_t>(require_u64(args, key));
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

std::vector<uint64_t> parse_u64_csv(const std::string& text, const std::string& key) {
  std::vector<uint64_t> values;
  if (text.empty()) return values;

  std::stringstream stream(text);
  std::string part;
  while (std::getline(stream, part, ',')) {
    if (part.empty()) {
      throw CliError("USAGE", "--" + key + " contains an empty item", EXIT_USAGE);
    }
    size_t consumed = 0;
    uint64_t value = 0;
    try {
      value = std::stoull(part, &consumed, 10);
    } catch (...) {
      throw CliError("USAGE", "--" + key + " must be a comma-separated unsigned integer list", EXIT_USAGE);
    }
    if (consumed != part.size()) {
      throw CliError("USAGE", "--" + key + " must be a comma-separated unsigned integer list", EXIT_USAGE);
    }
    values.push_back(value);
  }
  return values;
}

std::vector<size_t> parse_size_csv(const std::string& text, const std::string& key) {
  const auto raw = parse_u64_csv(text, key);
  return std::vector<size_t>(raw.begin(), raw.end());
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

std::string append_json_bool_field(std::string json_object, const std::string& key, bool value) {
  if (json_object.empty() || json_object.back() != '}') {
    throw CliError("JSON_BUILD_FAILED", "result JSON is not an object", EXIT_PARSE);
  }
  json_object.pop_back();
  json_object += ",";
  json_object += opengil::json::quote(key);
  json_object += ":";
  json_object += opengil::json::bool_value(value);
  json_object += "}";
  return json_object;
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
      << "\"validationKind\":\"structural\","
      << "\"semanticValidation\":\"notPerformed\","
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

  std::string result = opengil::cli::set_model_summary_to_json(mutation.model_summary);
  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
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

  std::string result = opengil::cli::projectile_motion_summary_to_json(mutation.summary);
  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
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
    const auto result = opengil::cli::custom_variables_list_to_json(rows);
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

  std::string result = opengil::cli::custom_vars_summary_to_json(mutation.summary);
  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
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

  std::string result = opengil::cli::decoration_summary_to_json(mutation.summary);
  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
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
    result = opengil::cli::attachment_summary_to_json(mutation.summary);
    command_name = "attachment.add";
  } else {
    mutation = opengil::add_attachment_points_from_decorations(file, prefab_id, object_id);
    result = opengil::cli::attachment_from_decoration_summary_to_json(mutation.summary);
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
    result = append_json_bool_field(std::move(result), "dryRun", true);
  }
  const auto json = envelope(command_name, true, file_input_json(file), output_json, result, {}, {});
  write_report_if_requested(args, json);
  return json;
}

std::string handle_ui(const Args& args) {
  if (args.positional.empty()) {
    throw CliError("USAGE", "ui requires a subcommand", EXIT_USAGE);
  }
  const std::string subcommand = args.positional[0];

  const auto input_path = std::filesystem::path(require_value(args, "input"));
  GilFile file = opengil::load_gil_file(input_path);
  const uint64_t controller_entry_id = optional_u64(args, "controller-entry-id")
      .value_or(opengil::kDefaultUiPrimitiveControllerEntryId);
  if (subcommand == "list") {
    const auto list = opengil::list_ui_primitives(file, controller_entry_id);
    const auto result = opengil::cli::ui_primitive_list_to_json(list);
    const auto json = envelope("ui.list", true, file_input_json(file), "null", result, {}, {});
    write_report_if_requested(args, json);
    return json;
  }

  const auto output_path = resolve_write_output_path(args, input_path);
  const bool dry_run = args.flags.contains("dry-run");

  std::string command_name;
  std::string result;
  std::vector<uint8_t> output_bytes;

  if (subcommand == "set-type") {
    const auto mutation = opengil::set_ui_primitive_type(file, require_size(args, "primitive-index"), require_u64(args, "type-id"), controller_entry_id);
    command_name = "ui.set-type";
    result = opengil::cli::ui_primitive_patch_summary_to_json(mutation.summary);
    output_bytes = mutation.bytes;
  } else if (subcommand == "set-color") {
    const auto mutation = opengil::set_ui_primitive_color(file, require_size(args, "primitive-index"), require_i64(args, "color"), controller_entry_id);
    command_name = "ui.set-color";
    result = opengil::cli::ui_primitive_patch_summary_to_json(mutation.summary);
    output_bytes = mutation.bytes;
  } else if (subcommand == "set-transform") {
    const auto mutation = opengil::set_ui_primitive_transform(file, require_size(args, "primitive-index"), ui_transform_from_args(args), controller_entry_id);
    command_name = "ui.set-transform";
    result = opengil::cli::ui_primitive_patch_summary_to_json(mutation.summary);
    output_bytes = mutation.bytes;
  } else if (subcommand == "set-layer") {
    const auto mutation = opengil::set_ui_primitive_layer(file, require_size(args, "primitive-index"), require_u64(args, "layer"), controller_entry_id);
    command_name = "ui.set-layer";
    result = opengil::cli::ui_primitive_patch_summary_to_json(mutation.summary);
    output_bytes = mutation.bytes;
  } else if (subcommand == "set-name") {
    const auto mutation = opengil::set_ui_primitive_name(file, require_size(args, "primitive-index"), require_value(args, "name"), controller_entry_id);
    command_name = "ui.set-name";
    result = opengil::cli::ui_primitive_patch_summary_to_json(mutation.summary);
    output_bytes = mutation.bytes;
  } else if (subcommand == "delete") {
    const auto primitive_indexes = parse_size_csv(require_value(args, "primitive-indexes"), "primitive-indexes");
    opengil::UiDeleteOptions options;
    options.target_controller_entry_id = optional_u64(args, "target-controller-entry-id")
        ? optional_u64(args, "target-controller-entry-id")
        : optional_u64(args, "controller-entry-id");
    const auto mutation = opengil::delete_ui_primitives(file, primitive_indexes, options);
    command_name = "ui.delete";
    result = opengil::cli::ui_structure_summary_to_json(mutation.summary);
    output_bytes = mutation.bytes;
  } else if (subcommand == "import-pixel") {
    opengil::UiPixelImportOptions options;
    options.pixel_size = require_double(args, "pixel-size");
    options.target_controller_entry_id = 1073741855;
    const auto mutation = opengil::import_pixel_png_as_ui_primitives(
        file,
        std::filesystem::path(require_value(args, "png")),
        options);
    command_name = "ui.import-pixel";
    result = opengil::cli::ui_structure_summary_to_json(mutation.summary);
    output_bytes = mutation.bytes;
  } else {
    throw CliError("USAGE", "unsupported ui subcommand: " + subcommand, EXIT_USAGE);
  }

  std::string output_json = "null";
  if (!dry_run) {
    if (output_path.empty()) {
      throw CliError("USAGE", "write output path resolved empty", EXIT_USAGE);
    }
    write_output_bytes(args, output_path, output_bytes);
    output_json = output_file_json(output_path, output_bytes);
  }

  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
  }
  const auto json = envelope(command_name, true, file_input_json(file), output_json, result, {}, {});
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
    result = opengil::cli::attach_all_nodegraphs_summary_to_json(mutation.summary);
  } else {
    const uint64_t nodegraph_id = require_u64(args, "nodegraph-id");
    const auto mutation = opengil::attach_nodegraph_to_prefab(file, prefab_id, nodegraph_id);
    bytes = mutation.bytes;
    result = opengil::cli::attach_nodegraph_summary_to_json(mutation.summary);
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
    result = append_json_bool_field(std::move(result), "dryRun", true);
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

  std::string result = opengil::cli::rename_prefab_summary_to_json(mutation.summary);
  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
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

  std::string result = opengil::cli::delete_prefab_summary_to_json(mutation.summary);
  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
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

opengil::UiPrimitiveTransform ui_transform_from_args(const Args& args) {
  opengil::UiPrimitiveTransform transform;
  transform.position.x = optional_double(args, "pos-x");
  transform.position.y = optional_double(args, "pos-y");
  transform.size.x = optional_double(args, "width");
  transform.size.y = optional_double(args, "height");
  transform.scale.x = optional_double(args, "scale-x");
  transform.scale.y = optional_double(args, "scale-y");
  transform.scale.z = optional_double(args, "scale-z");
  transform.rotation_z = optional_double(args, "rot-z");
  return transform;
}

opengil::DecorationSpec decoration_spec_from_args(const Args& args) {
  opengil::DecorationSpec spec;
  spec.asset_id = require_u64(args, "asset-id");
  spec.name = require_value(args, "name");
  spec.transform = transform_from_args(args);
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

  std::string result = opengil::cli::object_summary_to_json(mutation.summary);
  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
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
      ? opengil::cli::copy_prefab_summary_to_json(mutation.summary)
      : opengil::cli::clone_prefab_summary_to_json(mutation.summary);
  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
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

  std::string result = opengil::cli::object_summary_to_json(mutation.summary);
  if (dry_run) {
    result = append_json_bool_field(std::move(result), "dryRun", true);
  }
  const auto json = envelope(args.command, true, file_input_json(file), output_json, result, {}, {});
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
    result = opengil::cli::tabs_to_json(opengil::list_tabs(file));
  } else if (args.command == "list-prefabs") {
    const auto tab = value_or_empty(args, "tab");
    result = opengil::cli::prefabs_to_json(opengil::list_prefabs(file, tab.empty() ? std::nullopt : std::optional<std::string>(tab)));
  } else if (args.command == "list-prefab-tabs") {
    const uint64_t prefab_id = require_u64(args, "prefab-id");
    result = opengil::cli::prefab_tabs_to_json(opengil::list_prefab_tabs(file, prefab_id));
  } else if (args.command == "get-model") {
    const uint64_t prefab_id = require_u64(args, "prefab-id");
    const auto model = opengil::get_model_info(file, prefab_id);
    if (!model) {
      throw CliError("PREFAB_NOT_FOUND", "prefab id not found", EXIT_SEMANTIC);
    }
    result = opengil::cli::model_info_to_json(*model);
  } else if (args.command == "list-scene-objects") {
    result = opengil::cli::scene_objects_to_json(opengil::list_scene_objects(file));
  } else if (args.command == "list-preview-objects") {
    result = opengil::cli::scene_objects_to_json(opengil::list_preview_objects(file));
  } else if (args.command == "list-nodegraphs") {
    result = opengil::cli::nodegraphs_to_json(opengil::list_nodegraphs(file));
  } else {
    throw CliError(
        "NOT_IMPLEMENTED",
        "unknown or unsupported command",
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
