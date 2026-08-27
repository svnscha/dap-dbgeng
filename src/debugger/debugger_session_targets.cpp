#include "debugger/debugger_session.h"

#include "debugger/debugger_session_internal.h"

#include <tlhelp32.h>

namespace dap_dbgeng::debugger
{
namespace
{
std::string narrow_utf8(const wchar_t *wide)
{
    if (wide == nullptr || wide[0] == L'\0')
    {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1)
    {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

// Local processes via a toolhelp snapshot: it yields an image name for every
// process without opening it, which the engine's process-description API often
// cannot do for an unelevated debugger (it returns empty names there).
std::vector<process_info> snapshot_local_processes()
{
    std::vector<process_info> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            process_info info;
            info.system_id = entry.th32ProcessID;
            info.name = narrow_utf8(entry.szExeFile);
            result.push_back(std::move(info));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}
} // namespace
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

    // Process servers need the engine paired with its own dbghelp. When that
    // could not be loaded the engine falls back to the one in System32 and the
    // connection fails deep inside with an opaque error, so say it up front.
    // The packaged (Store) WinDbg is the case that hits this: it does not allow
    // loading DLLs out of its install directory.
    if (dbghelp_module_ == nullptr)
    {
        throw std::runtime_error(
            "This debug engine cannot be used for remote (dbgsrv) debugging: its dbghelp.dll could not be loaded, "
            "which the engine needs for process-server connections. The Store version of WinDbg does not allow it. "
            "Install the Debugging Tools for Windows and point 'dbgengPath' at that dbgeng.dll (or omit it).");
    }

    ULONG64 server = 0;
    check_hr(client_->ConnectProcessServer(connection_string.c_str(), &server),
             fmt::format("Could not connect to the process server '{}'", connection_string));
    process_server_handle_ = server;
}

// Runs as part of disposal, so it tolerates a session that is already torn down.
void debugger_session::disconnect_process_server()
{
    if (process_server_handle_ == 0 || client_ == nullptr)
    {
        return;
    }
    client_->DisconnectProcessServer(process_server_handle_);
    process_server_handle_ = 0;
}

std::vector<process_info> debugger_session::list_processes()
{
    throw_if_disposed();

    // Local: the toolhelp snapshot reliably reports image names. Remote: only the
    // engine can reach the dbgsrv host, so use its process-description API.
    if (process_server_handle_ == 0)
    {
        return snapshot_local_processes();
    }

    // process_server_handle_ is the connected dbgsrv handle for a remote host.
    // Query the count first, then read the ids.
    ULONG count = 0;
    if (FAILED(client_->GetRunningProcessSystemIds(process_server_handle_, nullptr, 0, &count)) || count == 0)
    {
        return {};
    }

    std::vector<ULONG> ids(count);
    check_hr(client_->GetRunningProcessSystemIds(process_server_handle_, ids.data(), count, &count),
             "Could not enumerate running processes");
    ids.resize(count);

    std::vector<process_info> result;
    result.reserve(ids.size());
    std::vector<char> exe_name(kSymbolBufferBytes);
    for (const ULONG id : ids)
    {
        process_info info;
        info.system_id = id;

        // Read the executable path (the verbose description - command line,
        // services, session - is deliberately ignored) and reduce it to the
        // basename ourselves. We take the full path rather than asking the engine
        // for the basename (NO_PATHS), whose remote path-stripping truncates some
        // names. This makes the remote picker match the local one: name + pid only.
        ULONG exe_size = 0;
        if (SUCCEEDED(client_->GetRunningProcessDescription(process_server_handle_, id, DEBUG_PROC_DESC_DEFAULT,
                                                            exe_name.data(), static_cast<ULONG>(exe_name.size()),
                                                            &exe_size, nullptr, 0, nullptr)) &&
            exe_size > 1)
        {
            info.name = std::filesystem::path(std::string(exe_name.data())).filename().string();
        }
        result.push_back(std::move(info));
    }
    return result;
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

void debugger_session::launch_remote(const std::string &connection_string, const std::string &executable_path,
                                     std::optional<std::string> arguments, std::optional<std::string> working_directory)
{
    throw_if_disposed();
    throw_if_null_or_whitespace(executable_path, "executablePath");

    connect_process_server(connection_string);

    DEBUG_CREATE_PROCESS_OPTIONS options{};
    options.CreateFlags = DEBUG_ONLY_THIS_PROCESS;

    // executable_path and cwd are paths on the process-server host; with no cwd
    // the engine defaults it there, so nothing is resolved locally.
    std::string command_line = build_command_line(executable_path, arguments);
    std::string initial_directory;
    if (working_directory && !trim_both(*working_directory).empty())
    {
        initial_directory = *working_directory;
    }

    check_hr(client_->CreateProcessAndAttach2(
                 process_server_handle_, command_line.data(), &options, static_cast<ULONG>(sizeof(options)),
                 initial_directory.empty() ? nullptr : initial_directory.c_str(), nullptr, 0, DEBUG_ATTACH_DEFAULT),
             fmt::format("Could not launch target on the process server '{}'", connection_string));
    wait_for_event();
}

void debugger_session::attach(int process_id)
{
    throw_if_disposed();
    check_hr(control_->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK), "Could not enable initial break");
    // process_server_handle_ is 0 unless a process server was connected, which
    // is exactly what a local attach needs.
    attach_process_native(process_server_handle_, process_id);
    wait_for_event();
}

std::optional<std::uint32_t> debugger_session::try_find_process_id_by_executable_name(
    const std::string &executable_name)
{
    throw_if_disposed();
    ULONG id = 0;
    const HRESULT hr = client_->GetRunningProcessSystemIdByExecutableName(
        process_server_handle_, executable_name.c_str(), DEBUG_GET_PROC_DEFAULT, &id);
    if (FAILED(hr) || id == 0)
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(id);
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
    connect_process_server(connection_string);
    attach(process_id);
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

    // AttachKernel only configures the transport; the connection happens when
    // the target sends its first packet, and this waits for it. A target that
    // never answers therefore hangs the attach request and holds the dispatcher
    // thread with it - see the note in docs/development/f5-experience.md. The
    // wait cannot simply be bounded: the engine rejects a finite timeout here
    // with E_NOTIMPL, kernel WaitForEvent only accepts INFINITE.
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
