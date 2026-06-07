#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_step_out_request(const protocol::StepOutRequest &request)
{
    const int thread_id = request.arguments.thread_id;
    const bool single_thread = request.arguments.single_thread.value_or(false);
    const std::optional<protocol::SteppingGranularity> granularity = request.arguments.granularity;

    if (!require_positive_thread_id(request.seq, request.command, thread_id))
    {
        return;
    }
    if (!reject_single_thread(request.seq, request.command, single_thread))
    {
        return;
    }
    if (is_execution_running_.load())
    {
        send_error_response(request.seq, request.command, "Cannot step while the debuggee is running.");
        return;
    }

    debugger::debugger_session &session = require_debugger_session();

    try
    {
        dispatcher_.invoke([&]() {
            session.set_current_thread(static_cast<std::uint32_t>(thread_id));
            return 0;
        });
        prepare_stepping_granularity(session, granularity);
        set_pending_stopped_event(protocol::StoppedEventBodyReason::Step, "Step out completed.");
        is_execution_running_.store(true);
        dispatcher_.invoke([&]() {
            session.step_out();
            return 0;
        });
        reset_suspended_state();
        start_waiting_for_session_event(session);
    }
    catch (...)
    {
        is_execution_running_.store(false);
        clear_pending_stopped_event();
        restore_source_line_stepping_if_needed(session);
        throw;
    }

    send_response(request.seq, request.command, protocol::StepOutResponse{});
}
} // namespace dap_dbgeng::service
