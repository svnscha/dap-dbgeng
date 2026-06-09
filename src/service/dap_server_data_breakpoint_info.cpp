#include "service/dap_server.h"

#include "util/string_utils.h"

namespace dap_dbgeng::service
{
void dap_server::handle_data_breakpoint_info_request(const protocol::DataBreakpointInfoRequest &request)
{
    const std::string &name = request.arguments.name;
    if (util::is_blank(name))
    {
        send_error_response(request.seq, request.command, "The dataBreakpointInfo request requires a non-empty name.");
        return;
    }

    // Resolve the target to a frame + expression: a child of a variable
    // container (the cached evaluateName is the full access path), or a bare
    // expression in a stack frame.
    std::uint32_t frame_number = 0;
    int thread_id = 0;
    std::string expression = name;
    if (request.arguments.variables_reference.has_value())
    {
        const auto container_it = variables_by_reference_.find(*request.arguments.variables_reference);
        if (container_it == variables_by_reference_.end())
        {
            send_error_response(request.seq, request.command,
                                fmt::format("Unknown variablesReference '{}'. Request scopes again for the current "
                                            "stop.",
                                            *request.arguments.variables_reference));
            return;
        }
        const variable_container_context &container = container_it->second;
        frame_number = container.frame_number;
        thread_id = container.thread_id;
        for (const protocol::Variable &variable : container.variables)
        {
            if (variable.name == name && variable.evaluate_name.has_value())
            {
                expression = *variable.evaluate_name;
                break;
            }
        }
    }
    else if (request.arguments.frame_id.has_value())
    {
        const auto context_it = frame_contexts_.find(*request.arguments.frame_id);
        if (context_it == frame_contexts_.end())
        {
            send_error_response(request.seq, request.command,
                                fmt::format("Unknown frame id '{}'. Request stackTrace again for the current stop.",
                                            *request.arguments.frame_id));
            return;
        }
        frame_number = context_it->second.frame_number;
        thread_id = context_it->second.thread_id;
    }
    else
    {
        send_error_response(request.seq, request.command,
                            "The dataBreakpointInfo request requires a variablesReference or a frameId.");
        return;
    }

    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command,
                            "The dataBreakpointInfo request requires an active debugger session.");
        return;
    }

    try
    {
        debugger::debugger_session &session = require_debugger_session();
        const std::pair<std::uint64_t, std::uint32_t> location = dispatcher_.invoke([&]() {
            const std::uint32_t original_thread_id = session.get_current_thread_id();
            try
            {
                session.set_current_thread(static_cast<std::uint32_t>(thread_id));
                std::pair<std::uint64_t, std::uint32_t> result = session.get_symbol_address(frame_number, expression);
                session.set_current_thread(original_thread_id);
                return result;
            }
            catch (...)
            {
                session.set_current_thread(original_thread_id);
                throw;
            }
        });

        // ba accepts only 1/2/4/8-byte ranges; clamp larger aggregates to 8
        // (break on a write to the first 8 bytes), the simple useful behavior.
        std::uint32_t size = location.second;
        if (size != 1 && size != 2 && size != 4 && size != 8)
        {
            size = 8;
        }

        protocol::DataBreakpointInfoResponse response;
        response.body.data_id = fmt::format("0x{:x}:{}", location.first, size);
        response.body.description = fmt::format("{} ({} bytes at 0x{:x})", expression, size, location.first);
        response.body.access_types = std::vector<protocol::DataBreakpointAccessType>{
            protocol::DataBreakpointAccessType::Write, protocol::DataBreakpointAccessType::ReadWrite};
        response.body.can_persist = false;
        send_response(request.seq, request.command, std::move(response));
    }
    catch (const std::exception &exception)
    {
        send_error_response(request.seq, request.command,
                            util::debugger_session_dispatcher::unwrap_failure_message(exception, exception.what()));
    }
}
} // namespace dap_dbgeng::service
