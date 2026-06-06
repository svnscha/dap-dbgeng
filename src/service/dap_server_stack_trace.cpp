#include "service/dap_server.h"

#include "util/debugger_session_dap_factory.h"

namespace dap_dbgeng::service
{
namespace factory = util::debugger_session_dap_factory;

void dap_server::handle_stack_trace_request(const protocol::StackTraceRequest &request)
{
    const protocol::StackTraceArguments &arguments = request.arguments;
    const int thread_id = arguments.thread_id;
    const stack_trace_window window = read_stack_trace_window(arguments);
    if (!require_positive_thread_id(request.seq, request.command, thread_id))
    {
        return;
    }

    const int fetch_limit = get_stack_trace_fetch_limit(window);
    const debugger::stack_trace_details details = read_session_data_or_default<>(
        [this, thread_id, fetch_limit](debugger::debugger_session &session) {
            return dispatcher_.invoke([&]() {
                session.set_current_thread(static_cast<std::uint32_t>(thread_id));
                return session.get_stack_trace_details(fetch_limit);
            });
        },
        debugger::stack_trace_details{});

    const stack_trace_result result = create_stack_trace_result(details.frames, details.total_frames, window);

    register_frames(thread_id, result.frames);

    protocol::StackTraceResponse response;
    response.body.stack_frames.reserve(result.frames.size());
    for (const auto &frame : result.frames)
    {
        response.body.stack_frames.push_back(factory::create_stack_frame(thread_id, frame));
    }
    response.body.total_frames = result.total_frames;
    send_response(request.seq, request.command, std::move(response));
}
} // namespace dap_dbgeng::service
