#include <fcntl.h>
#include <io.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "service/dap_server.h"
#include "transport/dap_trace_recorder.h"
#include "transport/io_stream_server.h"

int main()
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
