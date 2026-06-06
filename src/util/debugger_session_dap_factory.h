#pragma once

#include "debugger/debugger_types.h"

namespace dap_dbgeng::util
{
// Maps debugger-session state and events into Debug Adapter Protocol payloads.
namespace debugger_session_dap_factory
{
std::vector<protocol::Thread> create_threads(const std::vector<std::uint32_t> &thread_ids,
                                             std::optional<int> current_thread_id);

protocol::ProcessEvent create_process_event(const std::string &name, std::optional<int> process_id,
                                            protocol::ProcessEventBodyStartMethod start_method);

protocol::StoppedEvent create_stopped_event(protocol::StoppedEventBodyReason reason,
                                            std::optional<std::string> description, std::optional<int> thread_id,
                                            std::optional<std::vector<int>> hit_breakpoint_ids = std::nullopt);

protocol::ContinuedEvent create_continued_event(int thread_id, bool all_threads_continued);

protocol::OutputEvent create_output_event(
    const std::string &output, protocol::OutputEventBodyCategory category = protocol::OutputEventBodyCategory::Console);

protocol::ExitedEvent create_exited_event(int exit_code);

protocol::ThreadEvent create_thread_event(protocol::ThreadEventBodyReason reason, int thread_id);

protocol::StackFrame create_stack_frame(int thread_id, const debugger::stack_frame_info &frame);

protocol::DisassembledInstruction create_disassembled_instruction(
    const debugger::disassembled_instruction_info &instruction);

// Stable, non-zero positive frame id derived from (thread_id, frame_number).
// Deterministic within a process and across requests so a frame id minted by one
// stackTrace stays valid for later scopes/variables/evaluate (hash-combined,
// masked to a positive int, 0 -> 1).
int create_frame_id(int thread_id, std::uint32_t frame_number);

std::optional<protocol::Source> create_source(const std::optional<debugger::source_location> &source);

std::string format_address(std::uint64_t address);
} // namespace debugger_session_dap_factory
} // namespace dap_dbgeng::util
