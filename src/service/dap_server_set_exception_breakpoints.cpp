#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_set_exception_breakpoints_request(const protocol::SetExceptionBreakpointsRequest &request)
{
    // The single advertised filter is "cpp" (break on first-chance C++
    // exceptions); anything else is unknown and reported unverified.
    bool cpp_enabled = false;
    std::vector<protocol::Breakpoint> breakpoints;
    breakpoints.reserve(request.arguments.filters.size());
    for (const std::string &filter : request.arguments.filters)
    {
        protocol::Breakpoint breakpoint;
        if (filter == "cpp")
        {
            cpp_enabled = true;
            breakpoint.verified = true;
        }
        else
        {
            breakpoint.verified = false;
            breakpoint.message = fmt::format("Unknown exception filter '{}'.", filter);
        }
        breakpoints.push_back(std::move(breakpoint));
    }

    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command,
                            "The setExceptionBreakpoints request requires an active debugger session.");
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
                                "Exception filters can only be updated while the kernel target is halted. Pause "
                                "first.");
            return;
        }
        const auto apply = [&]() {
            dispatcher_.invoke([&]() {
                session.set_cpp_first_chance_break(cpp_enabled);
                return 0;
            });
        };
        if (resume_after_update)
        {
            apply_breakpoint_update_while_running(session, apply);
        }
        else
        {
            apply();
        }

        protocol::SetExceptionBreakpointsResponse response;
        protocol::SetExceptionBreakpointsResponseBody body;
        body.breakpoints = std::move(breakpoints);
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
