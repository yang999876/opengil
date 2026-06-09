#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "opengil/gil.hpp"
#include "opengil/json.hpp"
#include "opengil/model_ops.hpp"
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
    const auto parent = output_path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::ofstream stream(output_path, std::ios::binary);
    if (!stream) throw CliError("WRITE_FAILED", "failed to open output file: " + output_path.string(), EXIT_WRITE);
    stream.write(reinterpret_cast<const char*>(mutation.bytes.data()), static_cast<std::streamsize>(mutation.bytes.size()));
    if (!stream) throw CliError("WRITE_FAILED", "failed to write output file: " + output_path.string(), EXIT_WRITE);
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
