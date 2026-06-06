#include "util/debugger_session_dap_factory.h"

namespace dap_dbgeng::util::debugger_session_dap_factory
{
namespace
{
// The final path component.
std::string file_name(const std::string &path)
{
    return std::filesystem::path(path).filename().string();
}

// Convert to int, throwing on values that do not fit a 32-bit signed int.
int checked_to_int(std::uint32_t value)
{
    if (value > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    {
        throw std::overflow_error("Value was either too large or too small for an Int32.");
    }
    return static_cast<int>(value);
}
} // namespace

std::vector<protocol::Thread> create_threads(const std::vector<std::uint32_t> &thread_ids,
                                             std::optional<int> current_thread_id)
{
    std::vector<protocol::Thread> threads;
    threads.reserve(thread_ids.size());
    for (const std::uint32_t thread_id : thread_ids)
    {
        const int dap_thread_id = checked_to_int(thread_id);
        protocol::Thread thread;
        thread.id = dap_thread_id;
        thread.name = (current_thread_id.has_value() && *current_thread_id == dap_thread_id)
                          ? fmt::format("Thread {} (current)", thread_id)
                          : fmt::format("Thread {}", thread_id);
        threads.push_back(std::move(thread));
    }
    return threads;
}

protocol::ProcessEvent create_process_event(const std::string &name, std::optional<int> process_id,
                                            protocol::ProcessEventBodyStartMethod start_method)
{
    protocol::ProcessEvent event;
    event.body.name = name;
    event.body.system_process_id = process_id;
    event.body.is_local_process = true;
    event.body.start_method = start_method;
    return event;
}

protocol::StoppedEvent create_stopped_event(protocol::StoppedEventBodyReason reason,
                                            std::optional<std::string> description, std::optional<int> thread_id,
                                            std::optional<std::vector<int>> hit_breakpoint_ids)
{
    protocol::StoppedEvent event;
    event.body.reason = reason;
    event.body.description = std::move(description);
    event.body.thread_id = thread_id;
    event.body.all_threads_stopped = true;
    event.body.hit_breakpoint_ids = std::move(hit_breakpoint_ids);
    return event;
}

protocol::ContinuedEvent create_continued_event(int thread_id, bool all_threads_continued)
{
    protocol::ContinuedEvent event;
    event.body.thread_id = thread_id;
    event.body.all_threads_continued = all_threads_continued;
    return event;
}

protocol::OutputEvent create_output_event(const std::string &output, protocol::OutputEventBodyCategory category)
{
    protocol::OutputEvent event;
    event.body.category = category;
    event.body.output = output;
    return event;
}

protocol::ExitedEvent create_exited_event(int exit_code)
{
    protocol::ExitedEvent event;
    event.body.exit_code = exit_code;
    return event;
}

protocol::ThreadEvent create_thread_event(protocol::ThreadEventBodyReason reason, int thread_id)
{
    protocol::ThreadEvent event;
    event.body.reason = reason;
    event.body.thread_id = thread_id;
    return event;
}

protocol::StackFrame create_stack_frame(int thread_id, const debugger::stack_frame_info &frame)
{
    protocol::StackFrame stack_frame;
    stack_frame.id = create_frame_id(thread_id, frame.frame_number);
    const std::string name = frame.name.value_or(std::string{});
    const bool name_blank = name.find_first_not_of(" \t\r\n") == std::string::npos;
    stack_frame.name = name_blank ? format_address(frame.instruction_offset) : name;
    stack_frame.source = create_source(frame.source);
    stack_frame.line = frame.source.has_value() ? frame.source->line : 0;
    stack_frame.column = frame.source.has_value() ? 1 : 0;
    stack_frame.instruction_pointer_reference = format_address(frame.instruction_offset);
    return stack_frame;
}

protocol::DisassembledInstruction create_disassembled_instruction(
    const debugger::disassembled_instruction_info &instruction)
{
    protocol::DisassembledInstruction dap_instruction;
    dap_instruction.address = format_address(instruction.address);
    dap_instruction.instruction_bytes = instruction.instruction_bytes;
    dap_instruction.instruction = instruction.instruction;
    dap_instruction.symbol = instruction.symbol;
    dap_instruction.location = create_source(instruction.source);
    if (instruction.source.has_value())
    {
        dap_instruction.line = instruction.source->line;
        dap_instruction.column = 1;
    }
    if (instruction.is_invalid)
    {
        dap_instruction.presentation_hint = protocol::DisassembledInstructionPresentationHint::Invalid;
    }
    return dap_instruction;
}

int create_frame_id(int thread_id, std::uint32_t frame_number)
{
    // Deterministic 32-bit hash combine (FNV-1a style). Must be stable within a
    // process and yield the same id for the same (thread_id, frame_number) across
    // requests; the exact value is irrelevant (replay rewrites frame ids), only
    // stability and non-zero positivity matter (mask to a positive int, then
    // map 0 -> 1).
    std::uint32_t hash = 2166136261u;
    const auto mix = [&hash](std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte)
        {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= 16777619u;
        }
    };
    mix(static_cast<std::uint32_t>(thread_id));
    mix(frame_number);

    const int id = static_cast<int>(hash & static_cast<std::uint32_t>(std::numeric_limits<int>::max()));
    return id == 0 ? 1 : id;
}

std::optional<protocol::Source> create_source(const std::optional<debugger::source_location> &source)
{
    if (!source.has_value())
    {
        return std::nullopt;
    }
    protocol::Source dap_source;
    dap_source.name = file_name(source->path);
    dap_source.path = source->path;
    return dap_source;
}

std::string format_address(std::uint64_t address)
{
    return fmt::format("0x{:X}", address);
}
} // namespace dap_dbgeng::util::debugger_session_dap_factory
