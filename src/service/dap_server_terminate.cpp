#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_terminate_request(const protocol::TerminateRequest &request)
{
    if (debugger_session_ == nullptr)
    {
        send_response(request.seq, request.command, protocol::TerminateResponse{});
        send(nlohmann::json(protocol::TerminatedEvent{}));
        should_exit_ = true;
        return;
    }

    debugger::debugger_session &session = *debugger_session_;

    const bool previous_session_stop_or_exit_observed = session_stop_or_exit_observed_.load();
    // Reset before dispatch so a synchronous terminate event can set the flag and skip the wait path.
    session_stop_or_exit_observed_.store(false);
    try
    {
        // Unlike disconnect, terminate must target the debuggee even for attached sessions.
        dispatcher_.invoke([&]() {
            session.terminate_current_process();
            return 0;
        });
    }
    catch (...)
    {
        session_stop_or_exit_observed_.store(previous_session_stop_or_exit_observed);
        throw;
    }

    if (!session_stop_or_exit_observed_.load())
    {
        start_waiting_for_session_event(session);
    }

    send_response(request.seq, request.command, protocol::TerminateResponse{});
}
} // namespace dap_dbgeng::service
