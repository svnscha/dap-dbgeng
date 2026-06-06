#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_pause_request(const protocol::PauseRequest &request)
{
    const int thread_id = request.arguments.thread_id;

    if (thread_id <= 0)
    {
        send_error_response(request.seq, request.command, "The pause request requires a positive threadId.");
        return;
    }
    if (!is_execution_running_.load())
    {
        send_error_response(request.seq, request.command, "The debuggee is not running.");
        return;
    }

    debugger::debugger_session &session = require_debugger_session();

    try
    {
        // The target is running, so the dispatcher thread is blocked inside
        // wait_for_event. Break-in must NOT go through the dispatcher (it would
        // queue behind the blocked wait); interrupt() issues the engine's
        // thread-safe SetInterrupt off-dispatcher. We don't select a thread here.
        reset_suspended_state();
        set_pending_stopped_event(protocol::StoppedEventBodyReason::Pause, "Execution paused.");
        session.interrupt();
        start_waiting_for_session_event(session);
    }
    catch (...)
    {
        clear_pending_stopped_event();
        throw;
    }

    send_response(request.seq, request.command, protocol::PauseResponse{});
}
} // namespace dap_dbgeng::service
