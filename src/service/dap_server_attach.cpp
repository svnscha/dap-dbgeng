#include "service/dap_server.h"

#include "util/string_utils.h"

#include "util/dap_argument_reader.h"

namespace dap_dbgeng::service
{
namespace
{
namespace reader = util::dap_argument_reader;

constexpr auto kProcessPollInterval = std::chrono::milliseconds(200);
} // namespace

std::optional<std::uint32_t> dap_server::poll_for_process_id(debugger::debugger_session &session,
                                                             const std::string &executable_name,
                                                             std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
        const std::optional<std::uint32_t> found =
            dispatcher_.invoke([&]() { return session.try_find_process_id_by_executable_name(executable_name); });
        if (found.has_value() || std::chrono::steady_clock::now() >= deadline)
        {
            return found;
        }
        std::this_thread::sleep_for(kProcessPollInterval);
    }
}

void dap_server::handle_attach_request(const protocol::AttachRequest &request)
{
    const nlohmann::json arguments = reader::get_arguments(current_request_json_);
    const std::optional<std::string> engine_path_arg = reader::try_get_string(arguments, "dbgengPath");
    const std::optional<std::string> dump_file = reader::try_get_string(arguments, "dumpFile");
    std::optional<int> process_id = reader::try_get_int32(arguments, "processId");
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

        // Honor stopAtEntry like user-mode attach: the target is broken in either
        // way so breakpoints can be set; when stopAtEntry is false the stop is not
        // surfaced and configurationDone resumes the target instead.
        launch_awaiting_configuration_done_ = true;
        launch_stop_at_entry_ = stop_at_entry;
        launch_thread_id_ = try_get_current_thread_id();

        send_response(request.seq, request.command, protocol::AttachResponse{});
        send_process_event(fmt::format("kernel target {}", *connection_string), std::nullopt,
                           protocol::ProcessEventBodyStartMethod::Attach);
        is_execution_running_.store(false);
        if (stop_at_entry)
        {
            send_stopped_event(protocol::StoppedEventBodyReason::Pause, "Paused after kernel attach.");
        }
        return;
    }

    const std::optional<std::string> process_name = reader::try_get_string(arguments, "processName");
    if (util::is_blank(dump_file) && !process_id.has_value() && util::is_blank(process_name))
    {
        send_error_response(request.seq, request.command,
                            "The attach request requires 'processId', 'processName', or 'dumpFile'.");
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

    // Every target-connecting call runs on the dispatcher with session events
    // suppressed: the engine reports the attach break itself, which the handler
    // turns into the stopAtEntry-aware stop below.
    const auto connect_target = [&](auto &&action) {
        run_with_suppressed_session_events([&]() {
            dispatcher_.invoke([&]() {
                action();
                return 0;
            });
        });
    };

    if (!process_id.has_value())
    {
        // Attach by executable name. Connect the process server first, so the
        // lookup runs on the machine the process will appear on, then poll: the
        // process may be spawning right now (e.g. a service the client just
        // started).
        if (!util::is_blank(connection_string))
        {
            connect_target([&]() { live_session.connect_process_server(*connection_string); });
        }

        const int timeout_ms = std::max(reader::try_get_int32(arguments, "processNameTimeout").value_or(15000), 0);
        const std::optional<std::uint32_t> found =
            poll_for_process_id(live_session, *process_name, std::chrono::milliseconds(timeout_ms));
        if (!found.has_value())
        {
            send_error_response(request.seq, request.command,
                                fmt::format("No process named '{}' appeared within {} ms.", *process_name, timeout_ms));
            return;
        }
        process_id = static_cast<int>(*found);

        connect_target([&]() { live_session.attach(*process_id); });
    }
    else if (util::is_blank(connection_string))
    {
        connect_target([&]() { live_session.attach(*process_id); });
    }
    else
    {
        connect_target([&]() { live_session.attach_remote(*connection_string, *process_id); });
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
