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
        if (!util::try_apply_byte_offset(address, offset, address))
        {
            send_error_response(request.seq, request.command,
                                fmt::format("The offset {} moves '{}' outside the 64-bit address space.", offset,
                                            breakpoint.instruction_reference));
            return;
        }
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
        // While the target is running the dispatcher is parked in wait_for_event;
        // a plain invoke would block the transport thread behind it forever.
        const bool resume_after_update = is_execution_running_.load();
        if (resume_after_update && session.is_kernel())
        {
            send_error_response(request.seq, request.command,
                                "Breakpoints can only be updated while the kernel target is halted. Pause first.");
            return;
        }
        std::vector<debugger::source_breakpoint_result> results;
        const auto apply = [&]() {
            results = dispatcher_.invoke([&]() { return session.set_instruction_breakpoints(addresses); });
        };
        if (resume_after_update)
        {
            apply_breakpoint_update_while_running(session, apply);
        }
        else
        {
            apply();
        }

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
