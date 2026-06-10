#include "service/dap_server.h"

#include "util/string_utils.h"

namespace dap_dbgeng::service
{
void dap_server::handle_set_instruction_breakpoints_request(const protocol::SetInstructionBreakpointsRequest &request)
{
    // Parse every reference (plus its optional byte offset) before touching state.
    std::vector<std::uint64_t> addresses;
    addresses.reserve(request.arguments.breakpoints.size());
    for (const protocol::InstructionBreakpoint &breakpoint : request.arguments.breakpoints)
    {
        std::uint64_t address = 0;
        if (!util::try_parse_memory_reference(breakpoint.instruction_reference, address))
        {
            send_error_response(request.seq, request.command,
                                fmt::format("The instruction reference '{}' is not a valid address.",
                                            breakpoint.instruction_reference));
            return;
        }
        const std::int64_t offset = breakpoint.offset.value_or(0);
        if (offset < 0 && static_cast<std::uint64_t>(-offset) > address)
        {
            send_error_response(
                request.seq, request.command,
                fmt::format("The offset {} moves '{}' below address zero.", offset, breakpoint.instruction_reference));
            return;
        }
        address =
            offset >= 0 ? address + static_cast<std::uint64_t>(offset) : address - static_cast<std::uint64_t>(-offset);
        addresses.push_back(address);
    }
    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command,
                            "The setInstructionBreakpoints request requires an active debugger session.");
        return;
    }

    try
    {
        debugger::debugger_session &session = require_debugger_session();
        const std::vector<debugger::source_breakpoint_result> results =
            dispatcher_.invoke([&]() { return session.set_instruction_breakpoints(addresses); });

        protocol::SetInstructionBreakpointsResponse response;
        response.body.breakpoints.reserve(results.size());
        for (std::size_t i = 0; i < results.size(); ++i)
        {
            protocol::Breakpoint breakpoint;
            breakpoint.id = results[i].id;
            breakpoint.verified = results[i].verified;
            breakpoint.message = results[i].message;
            breakpoint.instruction_reference = fmt::format("0x{:x}", addresses[i]);
            response.body.breakpoints.push_back(std::move(breakpoint));
        }
        send_response(request.seq, request.command, std::move(response));
    }
    catch (const std::exception &exception)
    {
        send_error_response(request.seq, request.command,
                            util::debugger_session_dispatcher::unwrap_failure_message(exception, exception.what()));
    }
}
} // namespace dap_dbgeng::service
