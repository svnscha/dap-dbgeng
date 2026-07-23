#include "service/dap_server.h"

namespace dap_dbgeng::service
{
namespace
{
// Human-readable names for the exception codes a native debuggee commonly
// raises; anything else falls back to the hex code.
std::string describe_exception_code(std::uint32_t code)
{
    switch (code)
    {
    case 0xC0000005:
        return "Access violation";
    case 0xC0000094:
        return "Integer division by zero";
    case 0xC00000FD:
        return "Stack overflow";
    case 0xC0000409:
        return "Stack buffer overrun / fail fast";
    case 0xE06D7363:
        return "C++ exception";
    case 0x80000003:
        return "Break instruction";
    default:
        return fmt::format("Exception 0x{:08X}", code);
    }
}
} // namespace

void dap_server::handle_exception_info_request(const protocol::ExceptionInfoRequest &request)
{
    if (is_execution_running_.load())
    {
        send_error_response(request.seq, request.command,
                            "The debuggee is currently running. Exception details are only available while stopped.");
        return;
    }
    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command,
                            "The exceptionInfo request requires an active debugger session.");
        return;
    }

    try
    {
        debugger::debugger_session &session = require_debugger_session();
        const std::optional<debugger::last_exception_info> exception =
            dispatcher_.invoke([&]() { return session.get_last_exception(); });
        if (!exception.has_value())
        {
            send_error_response(request.seq, request.command, "No exception has been observed in this session.");
            return;
        }

        protocol::ExceptionInfoResponse response;
        response.body.exception_id = fmt::format("0x{:08X}", exception->code);
        response.body.description = describe_exception_code(exception->code);
        response.body.break_mode =
            exception->first_chance ? protocol::ExceptionBreakMode::Always : protocol::ExceptionBreakMode::Unhandled;
        protocol::ExceptionDetails details;
        details.message = fmt::format("{} at 0x{:x} ({} chance).", describe_exception_code(exception->code),
                                      exception->address, exception->first_chance ? "first" : "second");
        response.body.details = std::move(details);
        send_response(request.seq, request.command, std::move(response));
    }
    catch (const std::exception &exception)
    {
        send_error_response(request.seq, request.command,
                            util::debugger_session_dispatcher::unwrap_failure_message(exception, exception.what()));
    }
}
} // namespace dap_dbgeng::service
