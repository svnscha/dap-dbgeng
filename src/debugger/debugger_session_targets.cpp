#include "debugger/debugger_session.h"

#include "debugger/debugger_session_internal.h"

namespace dap_dbgeng::debugger
{
// ---------------------------------------------------------------------------
// Process server (dbgsrv)
// ---------------------------------------------------------------------------
std::uint64_t debugger_session::process_server_handle() const
{
    throw_if_disposed();
    return process_server_handle_;
}

void debugger_session::connect_process_server(const std::string &connection_string)
{
    throw_if_disposed();
    throw_if_null_or_whitespace(connection_string, "connectionString");

    ULONG64 server = 0;
    check_hr(client_->ConnectProcessServer(connection_string.c_str(), &server),
             fmt::format("Could not connect to the process server '{}'", connection_string));
    process_server_handle_ = server;
}

void debugger_session::disconnect_process_server()
{
    throw_if_disposed();
    disconnect_process_server_core();
}

void debugger_session::disconnect_process_server_core()
{
    if (process_server_handle_ == 0 || client_ == nullptr)
    {
        return;
    }
    client_->DisconnectProcessServer(process_server_handle_);
    process_server_handle_ = 0;
}

// ---------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------
void debugger_session::launch(const std::string &executable_path, std::optional<std::string> arguments,
                              std::optional<std::string> working_directory)
{
    throw_if_disposed();
    throw_if_null_or_whitespace(executable_path, "executablePath");

    DEBUG_CREATE_PROCESS_OPTIONS options{};
    options.CreateFlags = DEBUG_ONLY_THIS_PROCESS;

    std::string resolved_working_dir;
    if (working_directory && !trim_both(*working_directory).empty())
    {
        resolved_working_dir = *working_directory;
    }
    else
    {
        std::filesystem::path exe(executable_path);
        resolved_working_dir =
            exe.has_parent_path() ? exe.parent_path().string() : std::filesystem::current_path().string();
    }

    std::string command_line = build_command_line(executable_path, arguments);

    check_hr(client_->CreateProcessAndAttach2(0, command_line.data(), &options, static_cast<ULONG>(sizeof(options)),
                                              resolved_working_dir.c_str(), nullptr, 0, DEBUG_ATTACH_DEFAULT),
             "Could not launch target");
    wait_for_event();
}

void debugger_session::attach(int process_id)
{
    throw_if_disposed();
    check_hr(control_->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK), "Could not enable initial break");
    attach_process_native(0, process_id);
    wait_for_event();
}

void debugger_session::attach_process_native(std::uint64_t process_server_handle, int process_id)
{
    // DEBUG_ATTACH_DEFAULT (0) performs an invasive attach; INITIAL_BREAK breaks
    // in on the next wait. process_server_handle is 0 for a local target or the
    // ConnectProcessServer handle for a remote (dbgsrv) target.
    check_hr(client_->AttachProcess(process_server_handle, static_cast<ULONG>(process_id), DEBUG_ATTACH_DEFAULT),
             "Could not attach to target process");
}

void debugger_session::attach_remote(const std::string &connection_string, int process_id)
{
    throw_if_disposed();
    throw_if_null_or_whitespace(connection_string, "connectionString");

    check_hr(control_->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK), "Could not enable initial break");
    connect_process_server(connection_string);
    attach_process_native(process_server_handle_, process_id);
    wait_for_event();
}

void debugger_session::attach_kernel(const std::string &connection_string)
{
    throw_if_disposed();
    throw_if_null_or_whitespace(connection_string, "connectionString");

    // Enable INITIAL_BREAK before attaching so the engine sends the breakin
    // packet once the KDNET transport finishes connecting.
    check_hr(control_->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK), "Could not enable initial break");

    check_hr(client_->AttachKernel(DEBUG_ATTACH_KERNEL_CONNECTION, connection_string.c_str()),
             fmt::format("Could not attach to the kernel target '{}'", connection_string));

    // A kernel target cannot be terminated; disconnecting only drops the link.
    terminate_debuggee_on_dispose_ = false;
    is_kernel_ = true;

    wait_for_event();
}

void debugger_session::open_dump_file(const std::string &dump_file_path)
{
    throw_if_disposed();
    throw_if_null_or_whitespace(dump_file_path, "dumpFilePath");

    check_hr(client_->OpenDumpFile(dump_file_path.c_str()), "Could not open dump file");
    wait_for_event();
}
} // namespace dap_dbgeng::debugger
