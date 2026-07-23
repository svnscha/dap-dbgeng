#include "service/dap_server.h"

#include "util/string_utils.h"

namespace dap_dbgeng::service
{
void dap_server::handle_set_expression_request(const protocol::SetExpressionRequest &request)
{
    const std::string &expression = request.arguments.expression;
    const std::string &value = request.arguments.value;
    if (util::is_blank(expression))
    {
        send_error_response(request.seq, request.command, "The setExpression request requires a non-empty expression.");
        return;
    }
    if (util::is_blank(value))
    {
        send_error_response(request.seq, request.command, "The setExpression request requires a non-empty value.");
        return;
    }
    if (!request.arguments.frame_id.has_value())
    {
        send_error_response(request.seq, request.command,
                            "The setExpression request requires a frameId (global assignments are not supported).");
        return;
    }
    const auto context_it = frame_contexts_.find(*request.arguments.frame_id);
    if (context_it == frame_contexts_.end())
    {
        send_error_response(request.seq, request.command,
                            fmt::format("Unknown frame id '{}'. Request stackTrace again for the current stop.",
                                        *request.arguments.frame_id));
        return;
    }
    if (is_execution_running_.load())
    {
        send_error_response(
            request.seq, request.command,
            "The debuggee is currently running. Wait until execution stops before changing expressions.");
        return;
    }
    const frame_context context = context_it->second;

    try
    {
        debugger::debugger_session &session = require_debugger_session();
        // Assign through the symbol-group write (the same native path setVariable
        // uses for struct fields), on the dispatcher thread with the frame's thread.
        const debugger::variable_node updated = dispatcher_.invoke([&]() {
            const std::uint32_t original_thread_id = session.get_current_thread_id();
            try
            {
                session.set_current_thread(static_cast<std::uint32_t>(context.thread_id));
                debugger::variable_node node = session.set_local_value(context.frame_number, expression, value);
                session.set_current_thread(original_thread_id);
                return node;
            }
            catch (...)
            {
                session.set_current_thread(original_thread_id);
                throw;
            }
        });

        protocol::SetExpressionResponse response;
        response.body.value = updated.value;
        if (!updated.type.empty())
        {
            response.body.type = updated.type;
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
