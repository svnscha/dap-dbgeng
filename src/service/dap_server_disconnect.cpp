#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_disconnect_request(const protocol::DisconnectRequest &request)
{
    disconnect_request_options options;
    if (request.arguments.has_value())
    {
        options.restart = request.arguments->restart;
        options.terminate_debuggee = request.arguments->terminate_debuggee;
        options.suspend_debuggee = request.arguments->suspend_debuggee;
    }

    const bool should_send_terminated_event = debugger_session_ != nullptr && !session_stop_or_exit_observed_.load();
    disconnect_debugger_session(options);
    send_response(request.seq, request.command, protocol::DisconnectResponse{});
    if (should_send_terminated_event)
    {
        send(nlohmann::json(protocol::TerminatedEvent{}));
    }

    should_exit_ = true;
}
} // namespace dap_dbgeng::service
