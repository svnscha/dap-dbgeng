#include "service/dap_server.h"

#include "util/string_utils.h"

namespace dap_dbgeng::service
{
namespace
{
// A dataId is "0x<address>:<size>" as produced by dataBreakpointInfo.
bool try_parse_data_id(const std::string &data_id, std::uint64_t &address, std::uint32_t &size)
{
    const std::size_t separator = data_id.find(':');
    if (separator == std::string::npos)
    {
        return false;
    }
    if (!util::try_parse_memory_reference(data_id.substr(0, separator), address))
    {
        return false;
    }
    try
    {
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(data_id.substr(separator + 1), &consumed);
        if (consumed != data_id.size() - separator - 1)
        {
            return false;
        }
        size = static_cast<std::uint32_t>(parsed);
    }
    catch (const std::exception &)
    {
        return false;
    }
    return size == 1 || size == 2 || size == 4 || size == 8;
}
} // namespace

void dap_server::handle_set_data_breakpoints_request(const protocol::SetDataBreakpointsRequest &request)
{
    // Parse every dataId before touching state.
    std::vector<debugger::data_breakpoint_spec> specs;
    specs.reserve(request.arguments.breakpoints.size());
    for (const protocol::DataBreakpoint &breakpoint : request.arguments.breakpoints)
    {
        debugger::data_breakpoint_spec spec;
        if (!try_parse_data_id(breakpoint.data_id, spec.address, spec.size))
        {
            send_error_response(
                request.seq, request.command,
                fmt::format("The dataId '{}' is not valid. Request dataBreakpointInfo again for the current stop.",
                            breakpoint.data_id));
            return;
        }
        spec.break_on_read =
            breakpoint.access_type.has_value() && *breakpoint.access_type != protocol::DataBreakpointAccessType::Write;
        specs.push_back(spec);
    }
    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command,
                            "The setDataBreakpoints request requires an active debugger session.");
        return;
    }

    try
    {
        debugger::debugger_session &session = require_debugger_session();
        const std::vector<debugger::source_breakpoint_result> results =
            dispatcher_.invoke([&]() { return session.set_data_breakpoints(specs); });

        protocol::SetDataBreakpointsResponse response;
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
