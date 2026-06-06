#include "service/dap_server.h"

#include "util/string_utils.h"

#include "util/debugger_session_dap_factory.h"

namespace dap_dbgeng::service
{
namespace factory = util::debugger_session_dap_factory;

namespace
{
struct disassemble_arguments
{
    std::uint64_t memory_address = 0;
    int instruction_offset = 0;
    int instruction_count = 0;
    bool resolve_symbols = false;
};

bool try_parse_memory_reference(const std::string &memory_reference, std::uint64_t &address)
{
    std::string value = memory_reference;
    int base = 10;
    if (value.size() >= 2 && (value[0] == '0') && (value[1] == 'x' || value[1] == 'X'))
    {
        base = 16;
        value = value.substr(2);
    }
    // Strip WinDbg's `\`` group separators.
    value.erase(std::remove(value.begin(), value.end(), '`'), value.end());
    if (value.empty())
    {
        return false;
    }
    try
    {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed, base);
        if (consumed != value.size())
        {
            return false;
        }
        address = static_cast<std::uint64_t>(parsed);
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

bool try_apply_byte_offset(std::uint64_t memory_address, long long offset, std::uint64_t &adjusted)
{
    if (offset >= 0)
    {
        const std::uint64_t add = static_cast<std::uint64_t>(offset);
        if (memory_address > std::numeric_limits<std::uint64_t>::max() - add)
        {
            return false;
        }
        adjusted = memory_address + add;
        return true;
    }
    const std::uint64_t sub = static_cast<std::uint64_t>(-offset);
    if (memory_address < sub)
    {
        return false;
    }
    adjusted = memory_address - sub;
    return true;
}

bool try_read_disassemble_arguments(const protocol::DisassembleArguments &arguments, disassemble_arguments &result,
                                    std::string &error_message)
{
    error_message.clear();

    if (util::is_blank(arguments.memory_reference))
    {
        error_message = "The disassemble request requires a non-empty 'memoryReference'.";
        return false;
    }
    if (arguments.instruction_count < 0 || arguments.instruction_count > std::numeric_limits<int>::max())
    {
        error_message =
            "The disassemble request requires an 'instructionCount' that fits in a 32-bit integer and is not "
            "negative.";
        return false;
    }
    if (arguments.instruction_offset.has_value() && (*arguments.instruction_offset < std::numeric_limits<int>::min() ||
                                                     *arguments.instruction_offset > std::numeric_limits<int>::max()))
    {
        error_message = "The disassemble request 'instructionOffset' must fit in a 32-bit integer.";
        return false;
    }

    std::uint64_t memory_address = 0;
    if (!try_parse_memory_reference(arguments.memory_reference, memory_address))
    {
        error_message = fmt::format("The disassemble request 'memoryReference' value '{}' is not a valid address.",
                                    arguments.memory_reference);
        return false;
    }

    std::uint64_t adjusted = 0;
    if (!try_apply_byte_offset(memory_address, arguments.offset.value_or(0), adjusted))
    {
        error_message = "The disassemble request offset moves the target address out of range.";
        return false;
    }

    result.memory_address = adjusted;
    result.instruction_offset = static_cast<int>(arguments.instruction_offset.value_or(0));
    result.instruction_count = static_cast<int>(arguments.instruction_count);
    result.resolve_symbols = arguments.resolve_symbols.value_or(false);
    return true;
}
} // namespace

void dap_server::handle_disassemble_request(const protocol::DisassembleRequest &request)
{
    disassemble_arguments arguments;
    std::string error_message;
    if (!try_read_disassemble_arguments(request.arguments, arguments, error_message))
    {
        send_error_response(request.seq, request.command, error_message);
        return;
    }

    if (is_execution_running_.load())
    {
        send_error_response(request.seq, request.command,
                            "The debuggee is currently running. Wait until execution stops before disassembling code.");
        return;
    }

    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command,
                            "The disassemble request requires an active debugger session.");
        return;
    }

    try
    {
        std::vector<debugger::disassembled_instruction_info> instructions = read_session_data_or_default<>(
            [&](debugger::debugger_session &s) {
                return dispatcher_.invoke([&]() {
                    return s.disassemble(arguments.memory_address, arguments.instruction_offset,
                                         arguments.instruction_count, arguments.resolve_symbols);
                });
            },
            std::vector<debugger::disassembled_instruction_info>{});

        std::optional<debugger::stack_frame_info> current_frame = read_session_data_or_default<>(
            [&](debugger::debugger_session &s) -> std::optional<debugger::stack_frame_info> {
                std::vector<debugger::stack_frame_info> frames =
                    dispatcher_.invoke([&]() { return s.get_stack_trace(1); });
                if (frames.empty())
                {
                    return std::nullopt;
                }
                return frames.front();
            },
            std::optional<debugger::stack_frame_info>{});

        if (current_frame.has_value() && current_frame->source.has_value())
        {
            const debugger::source_location &current_source = *current_frame->source;
            for (auto &instruction : instructions)
            {
                if (instruction.address == current_frame->instruction_offset)
                {
                    instruction.source = current_source;
                }
            }
        }

        std::vector<protocol::DisassembledInstruction> dap_instructions;
        dap_instructions.reserve(instructions.size());
        for (const auto &instruction : instructions)
        {
            dap_instructions.push_back(factory::create_disassembled_instruction(instruction));
        }

        if (current_frame.has_value() && current_frame->source.has_value() && !dap_instructions.empty() &&
            !dap_instructions[0].location.has_value())
        {
            protocol::Source location;
            location.name = std::filesystem::path(current_frame->source->path).filename().string();
            location.path = current_frame->source->path;
            dap_instructions[0].location = location;
        }

        protocol::DisassembleResponse response;
        protocol::DisassembleResponseBody body;
        body.instructions = std::move(dap_instructions);
        response.body = std::move(body);
        send_response(request.seq, request.command, std::move(response));
    }
    catch (const std::exception &exception)
    {
        send_error_response(request.seq, request.command,
                            util::debugger_session_dispatcher::unwrap_failure_message(exception, exception.what()));
    }
}
} // namespace dap_dbgeng::service
