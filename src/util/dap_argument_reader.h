#pragma once

namespace dap_dbgeng::util
{
// Helpers for reading adapter-specific launch/attach arguments off raw DAP
// payloads. Forgiving readers that accept the loosely-typed values VS Code
// sends (numbers-as-strings, single-value-as-list) and return std::nullopt
// rather than throwing on a missing/mistyped field.
namespace dap_argument_reader
{
// The request's "arguments" object, or a null json value when absent/null.
nlohmann::json get_arguments(const nlohmann::json &request);

// Read a string property. Returns nullopt unless the value is a JSON string.
std::optional<std::string> try_get_string(const nlohmann::json &arguments, const std::string &key);

// Read an integer property. Accepts a JSON number or a numeric string.
std::optional<int> try_get_int32(const nlohmann::json &arguments, const std::string &key);

// Read a boolean property. Accepts a JSON bool or the strings "true"/"false".
std::optional<bool> try_get_boolean(const nlohmann::json &arguments, const std::string &key);

// Read a string list. Accepts a single string (wrapped into a one-element list)
// or an array (non-string elements dropped).
std::optional<std::vector<std::string>> try_get_string_list(const nlohmann::json &arguments, const std::string &key);

// Read "args" as a single command-line string: a string passes through; an
// array is shell-quoted token-by-token and joined with spaces.
std::optional<std::string> try_get_command_line_arguments(const nlohmann::json &arguments);

// Read "workingDir", or "" when absent/blank.
std::string resolve_working_directory(const nlohmann::json &arguments);
} // namespace dap_argument_reader
} // namespace dap_dbgeng::util
