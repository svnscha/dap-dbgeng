#include "service/dap_server.h"

#include "util/string_utils.h"

#include "util/windbg_command_classifier.h"

namespace dap_dbgeng::service
{
void dap_server::handle_evaluate_request(const protocol::EvaluateRequest &request)
{
    const protocol::EvaluateArguments &arguments = request.arguments;
    const std::string &expression = arguments.expression;

    if (expression.empty() || util::is_blank(expression))
    {
        send_error_response(request.seq, request.command, "The evaluate request requires a non-empty expression.");
        return;
    }
    if (arguments.column.has_value() && !arguments.line.has_value())
    {
        send_error_response(request.seq, request.command,
                            "The evaluate request requires line when column is provided.");
        return;
    }
    if (arguments.line.has_value() && !arguments.source.has_value())
    {
        send_error_response(request.seq, request.command,
                            "The evaluate request requires source when line is provided.");
        return;
    }

    std::optional<frame_context> context;
    if (arguments.frame_id.has_value())
    {
        const int frame_id = *arguments.frame_id;
        if (frame_id <= 0)
        {
            send_error_response(request.seq, request.command,
                                "The evaluate request requires a positive frameId when frameId is provided.");
            return;
        }
        const auto it = frame_contexts_.find(frame_id);
        if (it == frame_contexts_.end())
        {
            send_error_response(
                request.seq, request.command,
                fmt::format("Unknown frame id '{}'. Request stackTrace again for the current stop.", frame_id));
            return;
        }
        context = it->second;
    }

    if (arguments.context == protocol::EvaluateArgumentsContext::Hover)
    {
        protocol::EvaluateResponse response;
        response.body.result = std::string{};
        response.body.variables_reference = 0;
        send_response(request.seq, request.command, std::move(response));
        return;
    }

    if (is_execution_running_.load())
    {
        send_error_response(
            request.seq, request.command,
            "The debuggee is currently running. Wait until execution stops before evaluating commands.");
        return;
    }

    if (util::windbg_command_classifier::is_execution_control_command(expression))
    {
        send_error_response(request.seq, request.command,
                            "Execution-control WinDbg commands are not supported in evaluate. Use the DAP continue "
                            "and stepping requests instead.");
        return;
    }

    debugger::debugger_session &session = require_debugger_session();
    try
    {
        const std::string result = dispatcher_.invoke([&]() -> std::string {
            if (!context.has_value())
            {
                return session.execute_command_with_output(expression, /*suppress_output_events=*/true);
            }
            const std::uint32_t original_thread_id = session.get_current_thread_id();
            try
            {
                session.set_current_thread(static_cast<std::uint32_t>(context->thread_id));
                const std::string command = create_evaluate_command(expression, context->frame_number);
                std::string output = session.execute_command_with_output(command, /*suppress_output_events=*/true);
                session.set_current_thread(original_thread_id);
                return output;
            }
            catch (...)
            {
                session.set_current_thread(original_thread_id);
                throw;
            }
        });

        protocol::EvaluateResponse response;
        response.body.result = (result.empty() || util::is_blank(result)) ? "(no output)" : result;
        response.body.variables_reference = 0;
        send_response(request.seq, request.command, std::move(response));
    }
    catch (const std::exception &exception)
    {
        send_error_response(request.seq, request.command,
                            util::debugger_session_dispatcher::unwrap_failure_message(exception, exception.what()));
    }
}
} // namespace dap_dbgeng::service
