#pragma once

namespace dap_dbgeng::util
{
// Helpers for reading common DAP metadata off typed and raw messages.
namespace dap_message_inspector
{
// The request's command, falling back to the raw JSON when the typed command is
// blank.
std::string get_command(const protocol::Request &request);

// The "command" string of a raw message, or nullopt when absent/non-string.
std::optional<std::string> try_get_command(const nlohmann::json &message);

// The "seq" of a raw message, or nullopt when absent/non-integer.
std::optional<int> try_get_sequence(const nlohmann::json &message);
} // namespace dap_message_inspector
} // namespace dap_dbgeng::util
