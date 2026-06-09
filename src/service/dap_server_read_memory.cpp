#include "service/dap_server.h"

#include "util/base64.h"
#include "util/string_utils.h"

namespace dap_dbgeng::service
{
void dap_server::handle_read_memory_request(const protocol::ReadMemoryRequest &request)
{
    std::uint64_t address = 0;
    if (util::is_blank(request.arguments.memory_reference) ||
        !util::try_parse_memory_reference(request.arguments.memory_reference, address))
    {
        send_error_response(request.seq, request.command,
                            fmt::format("The readMemory request 'memoryReference' value '{}' is not a valid address.",
                                        request.arguments.memory_reference));
        return;
    }
    const std::int64_t count = request.arguments.count;
    if (count < 0 || count > (1 << 24))
    {
        send_error_response(request.seq, request.command,
                            "The readMemory request 'count' must be between 0 and 16 MiB.");
        return;
    }
    const std::int64_t offset = request.arguments.offset.value_or(0);
    if (offset < 0 && static_cast<std::uint64_t>(-offset) > address)
    {
        send_error_response(request.seq, request.command,
                            "The readMemory request 'offset' moves the address below zero.");
        return;
    }
    address =
        offset >= 0 ? address + static_cast<std::uint64_t>(offset) : address - static_cast<std::uint64_t>(-offset);

    if (is_execution_running_.load())
    {
        send_error_response(request.seq, request.command,
                            "The debuggee is currently running. Wait until execution stops before reading memory.");
        return;
    }
    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command,
                            "The readMemory request requires an active debugger session.");
        return;
    }

    try
    {
        const std::vector<unsigned char> bytes = read_session_data_or_default<>(
            [&](debugger::debugger_session &s) {
                return dispatcher_.invoke([&]() { return s.read_memory(address, static_cast<std::uint32_t>(count)); });
            },
            std::vector<unsigned char>{});

        protocol::ReadMemoryResponse response;
        protocol::ReadMemoryResponseBody body;
        body.address = fmt::format("0x{:x}", address);
        body.data = util::base64_encode(bytes);
        if (static_cast<std::int64_t>(bytes.size()) < count)
        {
            body.unreadable_bytes = count - static_cast<std::int64_t>(bytes.size());
        }
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
