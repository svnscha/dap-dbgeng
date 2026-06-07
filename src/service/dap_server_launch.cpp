#include "service/dap_server.h"

#include "util/string_utils.h"

#include "util/dap_argument_reader.h"

namespace dap_dbgeng::service
{
namespace
{
namespace reader = util::dap_argument_reader;

} // namespace

void dap_server::handle_launch_request(const protocol::LaunchRequest &request)
{
    // noDebug lives in the raw arguments (the generated LaunchRequestArguments is opaque).
    const nlohmann::json arguments = reader::get_arguments(current_request_json_);
    if (reader::try_get_boolean(arguments, "noDebug") == true)
    {
        send_error_response(request.seq, request.command, "Launching without debugging is not supported.");
        return;
    }

    const std::optional<std::string> executable_path = reader::try_get_string(arguments, "target");
    const std::optional<std::string> command_line_arguments = reader::try_get_command_line_arguments(arguments);
    const std::string working_directory = reader::resolve_working_directory(arguments);
    const std::optional<std::string> dbgeng_path = reader::try_get_string(arguments, "dbgengPath");
    const bool stop_at_entry = reader::try_get_boolean(arguments, "stopAtEntry").value_or(false);
    session_configuration configuration{reader::try_get_string_list(arguments, "symbolPath"),
                                        reader::try_get_string_list(arguments, "sources")};

    if (util::is_blank(executable_path))
    {
        send_error_response(request.seq, request.command, "The launch request requires a 'target' argument.");
        return;
    }

    // workingDir is optional: when omitted the engine defaults it to the target's
    // directory.
    std::string engine_path;
    if (!try_resolve_debugger_engine_path(dbgeng_path, engine_path))
    {
        send_error_response(request.seq, request.command,
                            "Could not locate dbgeng.dll. Set 'dbgengPath' to your dbgeng.dll, or install the "
                            "Windows SDK Debugging Tools.");
        return;
    }

    debugger::debugger_session &session = create_debugger_session(engine_path);
    apply_session_configuration(session, configuration);
    run_with_suppressed_session_events([&]() {
        dispatcher_.invoke([&]() {
            session.launch(*executable_path, command_line_arguments, working_directory);
            return 0;
        });
    });
    launch_awaiting_configuration_done_ = true;
    launch_stop_at_entry_ = stop_at_entry;
    launch_thread_id_ = try_get_current_thread_id();
    detach_on_disconnect_ = false;
    terminate_debuggee_on_disconnect_ = true;

    send_response(request.seq, request.command, protocol::LaunchResponse{});
    send_process_event(*executable_path, std::nullopt, protocol::ProcessEventBodyStartMethod::Launch);
    is_execution_running_.store(false);
    send_stopped_event(protocol::StoppedEventBodyReason::Entry, "Paused at process entry.");
}
} // namespace dap_dbgeng::service
