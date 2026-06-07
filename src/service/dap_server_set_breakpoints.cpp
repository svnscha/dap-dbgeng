#include "service/dap_server.h"

#include "util/string_utils.h"

#include "util/debugger_session_dap_factory.h"

namespace dap_dbgeng::service
{
namespace factory = util::debugger_session_dap_factory;

namespace
{
// A requested source breakpoint after parsing: either a configurable spec or an
// error (unsupported options / out-of-range line).
struct parsed_source_breakpoint
{
    protocol::SourceBreakpoint request;
    std::optional<debugger::source_breakpoint_spec> spec;
    std::optional<std::string> error_message;
    bool has_unsupported_options = false;
};

std::vector<protocol::SourceBreakpoint> read_requested_source_breakpoints(
    const protocol::SetBreakpointsArguments &arguments)
{
    if (arguments.breakpoints.has_value())
    {
        return *arguments.breakpoints;
    }
    if (arguments.lines.has_value())
    {
        std::vector<protocol::SourceBreakpoint> result;
        result.reserve(arguments.lines->size());
        for (const std::int64_t line : *arguments.lines)
        {
            protocol::SourceBreakpoint breakpoint;
            breakpoint.line = line;
            result.push_back(breakpoint);
        }
        return result;
    }
    return {};
}
} // namespace

protocol::Breakpoint dap_server::create_source_breakpoint_response(const protocol::Source &source,
                                                                   const protocol::SourceBreakpoint &breakpoint,
                                                                   bool verified, std::optional<std::string> message,
                                                                   std::optional<protocol::BreakpointReason> reason,
                                                                   std::optional<int> id)
{
    protocol::Breakpoint result;
    result.id = id;
    result.verified = verified;
    result.message = std::move(message);
    result.reason = reason;
    result.source = source;
    result.line = breakpoint.line;
    result.column = breakpoint.column;
    return result;
}

namespace
{
bool has_unsupported_options(const protocol::SourceBreakpoint &breakpoint)
{
    return breakpoint.column.has_value() || !util::is_blank(breakpoint.hit_condition) ||
           !util::is_blank(breakpoint.log_message) || !util::is_blank(breakpoint.mode);
}

parsed_source_breakpoint parse_source_breakpoint(const protocol::SourceBreakpoint &breakpoint)
{
    if (has_unsupported_options(breakpoint))
    {
        return parsed_source_breakpoint{breakpoint, std::nullopt,
                                        "Hit-count, logpoint, mode, and column source breakpoints are not supported.",
                                        true};
    }

    if (breakpoint.line <= 0 || breakpoint.line > std::numeric_limits<int>::max())
    {
        return parsed_source_breakpoint{breakpoint, std::nullopt, "Breakpoint lines must be between 1 and 2147483647.",
                                        false};
    }

    debugger::source_breakpoint_spec spec;
    spec.line = static_cast<int>(breakpoint.line);
    spec.condition = breakpoint.condition;
    return parsed_source_breakpoint{breakpoint, spec, std::nullopt, false};
}

std::string create_unsupported_breakpoint_message(const protocol::Source &source,
                                                  const std::vector<protocol::SourceBreakpoint> &breakpoints)
{
    const std::string source_label =
        util::is_blank(source.path) ? source.name.value_or("<unknown source>") : *source.path;
    std::set<std::int64_t> lines;
    for (const auto &breakpoint : breakpoints)
    {
        lines.insert(breakpoint.line);
    }
    std::string joined;
    for (auto it = lines.begin(); it != lines.end(); ++it)
    {
        if (it != lines.begin())
        {
            joined += ", ";
        }
        joined += std::to_string(*it);
    }
    return fmt::format("Rejected unsupported breakpoint options for {} at line(s) {}: hit-count, logpoint, mode, and "
                       "column source breakpoints are not supported.\r\n",
                       source_label, joined);
}
} // namespace

void dap_server::handle_set_breakpoints_request(const protocol::SetBreakpointsRequest &request)
{
    const protocol::SetBreakpointsArguments &arguments = request.arguments;
    const protocol::Source &source = arguments.source;
    const std::optional<int> source_reference = source.source_reference;
    const std::optional<std::string> &source_path = source.path;

    if (source_reference.has_value() && *source_reference > 0)
    {
        send_error_response(request.seq, request.command,
                            "The setBreakpoints request does not support 'source.sourceReference'; provide "
                            "'source.path'.");
        return;
    }
    if (util::is_blank(source_path))
    {
        send_error_response(request.seq, request.command, "The setBreakpoints request requires 'source.path'.");
        return;
    }

    std::vector<protocol::SourceBreakpoint> requested = read_requested_source_breakpoints(arguments);
    std::vector<parsed_source_breakpoint> parsed;
    parsed.reserve(requested.size());
    for (const auto &breakpoint : requested)
    {
        parsed.push_back(parse_source_breakpoint(breakpoint));
    }

    debugger::debugger_session &session = require_debugger_session();
    // User-mode targets accept breakpoint updates while running (the adapter
    // briefly interrupts to apply them, then resumes). A kernel target must be
    // halted first.
    const bool resume_after_update = is_execution_running_.load();
    if (resume_after_update && session.is_kernel())
    {
        send_error_response(request.seq, request.command,
                            "Breakpoints can only be updated while the kernel target is halted. Pause first.");
        return;
    }

    std::vector<protocol::SourceBreakpoint> unsupported_breakpoints;
    std::vector<parsed_source_breakpoint> configurable;
    for (const auto &breakpoint : parsed)
    {
        if (breakpoint.has_unsupported_options)
        {
            unsupported_breakpoints.push_back(breakpoint.request);
        }
        if (breakpoint.spec.has_value())
        {
            configurable.push_back(breakpoint);
        }
    }

    std::vector<debugger::source_breakpoint_spec> supported;
    supported.reserve(configurable.size());
    for (const auto &breakpoint : configurable)
    {
        supported.push_back(*breakpoint.spec);
    }

    const std::vector<debugger::source_breakpoint_result> configured =
        resume_after_update
            ? set_breakpoints_while_running(session, *source_path, supported)
            : dispatcher_.invoke([&]() { return session.set_source_breakpoints(*source_path, supported); });

    std::vector<protocol::SourceBreakpoint> configurable_requests;
    configurable_requests.reserve(configurable.size());
    for (const auto &breakpoint : configurable)
    {
        configurable_requests.push_back(breakpoint.request);
    }
    update_conditional_breakpoint_state(*source_path, configured, configurable_requests);

    std::vector<protocol::Breakpoint> response_breakpoints;
    response_breakpoints.reserve(parsed.size());
    std::size_t configured_index = 0;
    for (const auto &breakpoint : parsed)
    {
        if (breakpoint.error_message.has_value())
        {
            response_breakpoints.push_back(create_source_breakpoint_response(
                source, breakpoint.request, /*verified=*/false, breakpoint.error_message,
                protocol::BreakpointReason::Failed, std::nullopt));
            continue;
        }

        const debugger::source_breakpoint_result &result = configured[configured_index++];
        response_breakpoints.push_back(create_source_breakpoint_response(
            source, breakpoint.request, result.verified, result.message,
            result.verified ? std::optional<protocol::BreakpointReason>(std::nullopt)
                            : std::optional<protocol::BreakpointReason>(protocol::BreakpointReason::Failed),
            result.id));
    }

    protocol::SetBreakpointsResponse response;
    response.body.breakpoints = std::move(response_breakpoints);
    send_response(request.seq, request.command, std::move(response));

    if (!unsupported_breakpoints.empty())
    {
        send(nlohmann::json(
            factory::create_output_event(create_unsupported_breakpoint_message(source, unsupported_breakpoints),
                                         protocol::OutputEventBodyCategory::Important)));
    }
}
} // namespace dap_dbgeng::service
