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

#include "opengil/gil.hpp"
#include "opengil/custom_vars_ops.hpp"
#include "opengil/json.hpp"
#include "opengil/json_value.hpp"
#include "opengil/model_ops.hpp"
#include "opengil/nodegraph_ops.hpp"
#include "opengil/prefab_ops.hpp"
#include "opengil/projectile_ops.hpp"
#include "opengil/semantic.hpp"
#include "opengil/version.hpp"

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
  uint64_t source_prefab_id = 0;
  uint64_t target_prefab_id = 0;
  std::optional<uint64_t> asset_id;
  std::optional<uint64_t> nodegraph_id;
  std::optional<uint64_t> tab_id;
  std::optional<double> x;
  std::optional<double> y;
  std::optional<double> angle_deg;
  std::optional<double> speed;
  std::optional<double> gravity;
  std::string name;
  std::string type;
  std::string tab;
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

void write_bytes_to_path(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
  const auto parent = path.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);
  std::ofstream stream(path, std::ios::binary);
  if (!stream) throw CliError("WRITE_FAILED", "failed to open output file: " + path.string(), EXIT_WRITE);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!stream) throw CliError("WRITE_FAILED", "failed to write output file: " + path.string(), EXIT_WRITE);
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

std::string not_implemented_result(const Args& args) {
  const std::string result = "{}";
  return envelope(
      args.command,
      false,
      null_input_json(),
      "null",
      result,
      {},
      {error_json("NOT_IMPLEMENTED", "this command is planned but not implemented in the current openGil milestone")});
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
    if (op.op == "set-model") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.asset_id = require_json_u64(item, {"assetId", "asset-id", "modelAssetId", "model-asset-id"}, index);
    } else if (op.op == "set-empty-model") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
    } else if (op.op == "rename-prefab") {
      op.prefab_id = require_json_u64(item, {"prefabId", "prefab-id"}, index);
      op.name = require_json_string(item, {"name", "newName", "new-name"}, index);
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
    write_bytes_to_path(output_path, mutation.bytes);
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
    write_bytes_to_path(output_path, mutation.bytes);
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
    write_bytes_to_path(output_path, mutation.bytes);
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
    write_bytes_to_path(output_path, bytes);
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
    write_bytes_to_path(output_path, mutation.bytes);
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
    write_bytes_to_path(output_path, final_bytes);
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
    return not_implemented_result(args);
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
    } else if (args.command == "attach-nodegraph") {
      output = handle_attach_nodegraph(args, false);
    } else if (args.command == "attach-all-nodegraphs") {
      output = handle_attach_nodegraph(args, true);
    } else if (args.command == "set-projectile-motion") {
      output = handle_set_projectile_motion(args);
    } else if (args.command == "custom-vars") {
      output = handle_custom_vars(args);
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
