#include "service/dap_server.h"

#include "util/string_utils.h"

namespace dap_dbgeng::service
{
void dap_server::handle_set_function_breakpoints_request(const protocol::SetFunctionBreakpointsRequest &request)
{
    for (const protocol::FunctionBreakpoint &breakpoint : request.arguments.breakpoints)
    {
        if (util::is_blank(breakpoint.name))
        {
            send_error_response(request.seq, request.command, "Every function breakpoint requires a non-empty name.");
            return;
        }
    }
    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command,
                            "The setFunctionBreakpoints request requires an active debugger session.");
        return;
    }

    try
    {
        std::vector<std::string> names;
        names.reserve(request.arguments.breakpoints.size());
        for (const protocol::FunctionBreakpoint &breakpoint : request.arguments.breakpoints)
        {
            names.push_back(util::trim_both(breakpoint.name));
        }

        debugger::debugger_session &session = require_debugger_session();
        const std::vector<debugger::source_breakpoint_result> results =
            dispatcher_.invoke([&]() { return session.set_function_breakpoints(names); });

        protocol::SetFunctionBreakpointsResponse response;
        response.body.breakpoints.reserve(results.size());
        for (const debugger::source_breakpoint_result &result : results)
        {
            protocol::Breakpoint breakpoint;
            breakpoint.id = result.id;
            breakpoint.verified = result.verified;
            breakpoint.message = result.message;
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
