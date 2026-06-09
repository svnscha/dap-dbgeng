#include "service/dap_server.h"

#include "util/base64.h"
#include "util/string_utils.h"

namespace dap_dbgeng::service
{
void dap_server::handle_write_memory_request(const protocol::WriteMemoryRequest &request)
{
    std::uint64_t address = 0;
    if (util::is_blank(request.arguments.memory_reference) ||
        !util::try_parse_memory_reference(request.arguments.memory_reference, address))
    {
        send_error_response(request.seq, request.command,
                            fmt::format("The writeMemory request 'memoryReference' value '{}' is not a valid address.",
                                        request.arguments.memory_reference));
        return;
    }
    std::vector<unsigned char> bytes;
    if (!util::try_base64_decode(request.arguments.data, bytes))
    {
        send_error_response(request.seq, request.command, "The writeMemory request 'data' value is not valid base64.");
        return;
    }
    const std::int64_t offset = request.arguments.offset.value_or(0);
    if (offset < 0 && static_cast<std::uint64_t>(-offset) > address)
    {
        send_error_response(request.seq, request.command,
                            "The writeMemory request 'offset' moves the address below zero.");
        return;
    }
    address =
        offset >= 0 ? address + static_cast<std::uint64_t>(offset) : address - static_cast<std::uint64_t>(-offset);

    if (is_execution_running_.load())
    {
        send_error_response(request.seq, request.command,
                            "The debuggee is currently running. Wait until execution stops before writing memory.");
        return;
    }
    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command,
                            "The writeMemory request requires an active debugger session.");
        return;
    }

    try
    {
        debugger::debugger_session &session = require_debugger_session();
        const std::uint32_t written = dispatcher_.invoke([&]() { return session.write_memory(address, bytes); });

        protocol::WriteMemoryResponse response;
        protocol::WriteMemoryResponseBody body;
        body.bytes_written = static_cast<std::int64_t>(written);
        response.body = std::move(body);
        send_response(request.seq, request.command, std::move(response));
    }
    catch (const std::exception &exception)
    {
        send_error_response(request.seq, request.command,
                            util::debugger_session_dispatcher::unwrap_failure_message(exception, exception.what()));
    }
}
} // namespace dap_dbgeng::service
