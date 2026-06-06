#include "service/dap_server.h"

#include "util/debugger_session_dap_factory.h"

namespace dap_dbgeng::service
{
namespace factory = util::debugger_session_dap_factory;

void dap_server::handle_continue_request(const protocol::ContinueRequest &request)
{
    const int thread_id = request.arguments.thread_id;
    const bool single_thread = request.arguments.single_thread.value_or(false);

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
        send_error_response(request.seq, request.command, "The debuggee is already running.");
        return;
    }

    debugger::debugger_session &session = require_debugger_session();

    try
    {
        dispatcher_.invoke([&]() {
            session.set_current_thread(static_cast<std::uint32_t>(thread_id));
            return 0;
        });
        is_execution_running_.store(true);
        dispatcher_.invoke([&]() {
            session.continue_();
            return 0;
        });
        reset_suspended_state();
    }
    catch (...)
    {
        is_execution_running_.store(false);
        throw;
    }

    protocol::ContinueResponse response;
    response.body.all_threads_continued = true;
    send_response(request.seq, request.command, std::move(response));

    send(nlohmann::json(factory::create_continued_event(thread_id, /*all_threads_continued=*/true)));
    start_waiting_for_session_event(session);
}
} // namespace dap_dbgeng::service
