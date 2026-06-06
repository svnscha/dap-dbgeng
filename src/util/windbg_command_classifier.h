#pragma once

namespace dap_dbgeng::util
{
// Identifies WinDbg commands that would mutate execution state and conflict with
// the DAP request flow.
namespace windbg_command_classifier
{
// True when any `;`-separated segment's first token is an execution-control
// command (g/gh/gn/gc/p/pa/pc/ph/t/ta/tc/th/gu/.breakin), case-insensitive.
bool is_execution_control_command(const std::string &command);
} // namespace windbg_command_classifier
} // namespace dap_dbgeng::util
