#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_modules_request(const protocol::ModulesRequest &request)
{
    const int start = request.arguments.start_module.value_or(0);
    const std::int64_t count = request.arguments.module_count.value_or(0);
    if (start < 0 || count < 0)
    {
        send_error_response(request.seq, request.command,
                            "The modules request requires non-negative startModule and moduleCount.");
        return;
    }

    if (is_execution_running_.load())
    {
        send_error_response(request.seq, request.command,
                            "The debuggee is currently running. The module list is only available while stopped.");
        return;
    }
    if (debugger_session_ == nullptr)
    {
        send_error_response(request.seq, request.command, "The modules request requires an active debugger session.");
        return;
    }

    try
    {
        const std::vector<debugger::module_info> modules = read_session_data_or_default<>(
            [&](debugger::debugger_session &s) { return dispatcher_.invoke([&]() { return s.get_modules(); }); },
            std::vector<debugger::module_info>{});

        protocol::ModulesResponse response;
        response.body.total_modules = static_cast<std::int64_t>(modules.size());
        for (std::size_t index = static_cast<std::size_t>(start); index < modules.size(); ++index)
        {
            if (count > 0 && response.body.modules.size() >= static_cast<std::size_t>(count))
            {
                break;
            }
            const debugger::module_info &info = modules[index];
            protocol::Module module;
            // The base address is the engine's stable identity for a module.
            module.id = fmt::format("0x{:x}", info.base);
            module.name = info.name;
            module.path = info.image_path;
            module.symbol_status = info.symbol_status;
            module.address_range = fmt::format("0x{:x}-0x{:x}", info.base, info.base + info.size);
            response.body.modules.push_back(std::move(module));
        }
        send_response(request.seq, request.command, std::move(response));
    }
    catch (const std::exception &exception)
    {
        send_error_response(request.seq, request.command,
                            util::debugger_session_dispatcher::unwrap_failure_message(exception, exception.what()));
    }
}
} // namespace dap_dbgeng::service
