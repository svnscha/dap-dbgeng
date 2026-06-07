#include "service/dap_server.h"

#include "util/string_utils.h"

#include "util/dap_argument_reader.h"

namespace dap_dbgeng::service
{
namespace
{
namespace reader = util::dap_argument_reader;

} // namespace

void dap_server::handle_attach_request(const protocol::AttachRequest &request)
{
    const nlohmann::json arguments = reader::get_arguments(current_request_json_);
    const std::optional<std::string> engine_path_arg = reader::try_get_string(arguments, "dbgengPath");
    const std::optional<std::string> dump_file = reader::try_get_string(arguments, "dumpFile");
    const std::optional<int> process_id = reader::try_get_int32(arguments, "processId");
    const std::optional<std::string> connection_string = reader::try_get_string(arguments, "connectionString");
    const bool kernel = reader::try_get_boolean(arguments, "kernel").value_or(false);
    const bool stop_at_entry = reader::try_get_boolean(arguments, "stopAtEntry").value_or(true);
    session_configuration configuration{reader::try_get_string_list(arguments, "symbolPath"),
                                        reader::try_get_string_list(arguments, "sources")};

    std::string engine_path;
    if (!try_resolve_debugger_engine_path(engine_path_arg, engine_path))
    {
        send_error_response(request.seq, request.command,
                            "Could not locate dbgeng.dll. Set 'dbgengPath' to your dbgeng.dll, or install the "
                            "Windows SDK Debugging Tools.");
        return;
    }

    if (kernel)
    {
        if (util::is_blank(connection_string))
        {
            send_error_response(request.seq, request.command,
                                "Kernel attach requires a 'connectionString' transport, e.g. "
                                "'net:port=50000,key=...' or "
                                "'com:pipe,port=\\\\.\\pipe\\kd,resets=0,reconnect'.");
            return;
        }

        debugger::debugger_session &kernel_session = create_debugger_session(engine_path);
        apply_session_configuration(kernel_session, configuration);
        run_with_suppressed_session_events([&]() {
            dispatcher_.invoke([&]() {
                kernel_session.attach_kernel(*connection_string);
                return 0;
            });
        });
        detach_on_disconnect_ = false;
        terminate_debuggee_on_disconnect_ = false;
        kernel_initial_resume_pending_.store(true);

        send_response(request.seq, request.command, protocol::AttachResponse{});
        send_process_event(fmt::format("kernel target {}", *connection_string), std::nullopt,
                           protocol::ProcessEventBodyStartMethod::Attach);
        is_execution_running_.store(false);
        send_stopped_event(protocol::StoppedEventBodyReason::Pause, "Paused after kernel attach.");
        return;
    }

    if (util::is_blank(dump_file) && !process_id.has_value())
    {
        send_error_response(request.seq, request.command,
                            "The attach request requires either 'processId' or 'dumpFile'.");
        return;
    }

    if (!util::is_blank(dump_file))
    {
        debugger::debugger_session &session = create_debugger_session(engine_path);
        apply_session_configuration(session, configuration);
        run_with_suppressed_session_events([&]() {
            dispatcher_.invoke([&]() {
                session.open_dump_file(*dump_file);
                return 0;
            });
        });
        detach_on_disconnect_ = false;
        terminate_debuggee_on_disconnect_ = false;

        send_response(request.seq, request.command, protocol::AttachResponse{});
        send_process_event(*dump_file, std::nullopt, protocol::ProcessEventBodyStartMethod::Attach);
        is_execution_running_.store(false);
        send_stopped_event(protocol::StoppedEventBodyReason::Pause, "Paused after opening dump file.");
        return;
    }

    debugger::debugger_session &live_session = create_debugger_session(engine_path);
    apply_session_configuration(live_session, configuration);

    if (util::is_blank(connection_string))
    {
        run_with_suppressed_session_events([&]() {
            dispatcher_.invoke([&]() {
                live_session.attach(*process_id);
                return 0;
            });
        });
    }
    else
    {
        run_with_suppressed_session_events([&]() {
            dispatcher_.invoke([&]() {
                live_session.attach_remote(*connection_string, *process_id);
                return 0;
            });
        });
    }

    // Honor stopAtEntry like launch: attaching breaks the target in, and
    // configurationDone resumes it when stopAtEntry is false (defaults to true).
    launch_awaiting_configuration_done_ = true;
    launch_stop_at_entry_ = stop_at_entry;
    launch_thread_id_ = try_get_current_thread_id();
    detach_on_disconnect_ = true;
    terminate_debuggee_on_disconnect_ = false;

    send_response(request.seq, request.command, protocol::AttachResponse{});
    send_process_event(fmt::format("process {}", *process_id), process_id,
                       protocol::ProcessEventBodyStartMethod::Attach);
    is_execution_running_.store(false);

    // Only surface the stop when stopAtEntry is set (the default for attach).
    // Otherwise the target stays halted at the attach break so breakpoints can be
    // set, and configurationDone resumes it without a visible stop.
    if (stop_at_entry)
    {
        send_stopped_event(protocol::StoppedEventBodyReason::Pause, "Paused after attach.");
    }
}
} // namespace dap_dbgeng::service
