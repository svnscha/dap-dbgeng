#include <fcntl.h>
#include <io.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "debugger/debugger_session.h"
#include "service/dap_server.h"
#include "transport/dap_trace_recorder.h"
#include "transport/io_stream_server.h"

namespace
{
// Value following `name` in argv, or nullopt.
std::optional<std::string> flag_value(const std::vector<std::string> &args, const std::string &name)
{
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
    {
        if (args[i] == name)
        {
            return args[i + 1];
        }
    }
    return std::nullopt;
}

// `dap-dbgeng --list-processes [--dbgeng <path>] [--connection <dbgsrv string>]`
// enumerates running processes via the engine and prints a JSON array of
// { systemId, name, description } to stdout (or { "error": ... } on failure). With
// --connection it lists processes on the dbgsrv host; otherwise the local machine.
// Used by the VS Code extension's process picker; not part of the DAP transport.
int run_list_processes(const std::vector<std::string> &args)
{
    auto logger = spdlog::stderr_color_mt("dap-dbgeng");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::warn);

    try
    {
        std::string engine_path;
        if (!dap_dbgeng::service::dap_server::try_resolve_debugger_engine_path(flag_value(args, "--dbgeng"),
                                                                               engine_path))
        {
            std::cout << nlohmann::json{{"error", "Could not locate dbgeng.dll. Pass --dbgeng or install the "
                                                  "Windows SDK Debugging Tools."}}
                             .dump()
                      << std::endl;
            return 1;
        }
        const std::optional<std::string> connection = flag_value(args, "--connection");

        dap_dbgeng::debugger::debugger_session session(engine_path);
        if (connection && !connection->empty())
        {
            session.connect_process_server(*connection);
        }

        nlohmann::json processes = nlohmann::json::array();
        for (const auto &process : session.list_processes())
        {
            processes.push_back(
                {{"systemId", process.system_id}, {"name", process.name}, {"description", process.description}});
        }
        std::cout << processes.dump() << std::endl;
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cout << nlohmann::json{{"error", exception.what()}}.dump() << std::endl;
        return 1;
    }
}

int run_dap_server()
{
    // The DAP transport owns stdin/stdout, so all logging goes to stderr to
    // avoid corrupting the protocol stream.
    auto logger = spdlog::stderr_color_mt("dap-dbgeng");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    spdlog::info("dap-dbgeng starting");

    // DAP framing is byte-exact; keep the C++ streams off the C runtime's text
    // translation so CRLF in the protocol bytes is not mangled.
    std::ios::sync_with_stdio(false);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    // Tracing is opt-in via the debug configuration's "trace" path. The recorder
    // buffers from the start; the first launch/attach request either activates
    // recording to that path (keeping the buffered initialize/handshake) or, when
    // no trace is configured, discards the buffer so a normal run retains nothing.
    dap_dbgeng::transport::dap_trace_recorder trace_recorder;

    dap_dbgeng::transport::io_stream_server transport(std::cin, std::cout, &trace_recorder);
    dap_dbgeng::service::dap_server server(transport);
    transport.run(server);

    spdlog::info("dap-dbgeng exiting");
    spdlog::shutdown();
    return 0;
}
} // namespace

int main(int argc, char **argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (std::find(args.begin(), args.end(), "--list-processes") != args.end())
    {
        return run_list_processes(args);
    }
    return run_dap_server();
}
