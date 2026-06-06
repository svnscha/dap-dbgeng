#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_initialize_request(const protocol::InitializeRequest &request)
{
    if (initialize_request_handled_)
    {
        send_error_response(request.seq, request.command,
                            "The initialize request may only be sent once per debug adapter session.");
        return;
    }

    // Capability surface.
    protocol::Capabilities capabilities;
    capabilities.supports_clipboard_context = true;
    capabilities.supports_configuration_done_request = true;
    capabilities.supports_conditional_breakpoints = true;
    capabilities.supports_delayed_stack_trace_loading = true;
    capabilities.supports_disassemble_request = true;
    // Hover evaluation must remain side-effect free, but the adapter forwards arbitrary WinDbg commands.
    capabilities.supports_evaluate_for_hovers = false;
    capabilities.supports_function_breakpoints = false;
    capabilities.supports_hit_conditional_breakpoints = false;
    capabilities.supports_instruction_breakpoints = false;
    capabilities.supports_log_points = false;
    capabilities.supports_modules_request = false;
    capabilities.supports_read_memory_request = false;
    capabilities.supports_restart_request = false;
    capabilities.supports_restart_frame = false;
    capabilities.supports_step_back = false;
    capabilities.supports_set_variable = true;
    capabilities.supports_set_expression = false;
    // Note: SupportsVariablePaging exists only on the client InitializeRequestArguments in this
    // generated model, not on the server Capabilities struct (it is a client capability in the DAP
    // schema), so it is not advertised here. Behavior is unaffected (replay ignores the caps body)
    // and variable paging is still honored in the variables handler.
    capabilities.supports_single_thread_execution_requests = false;
    capabilities.supports_stepping_granularity = true;
    capabilities.supports_terminate_request = true;
    capabilities.supports_value_formatting_options = false;
    capabilities.supports_write_memory_request = false;
    capabilities.support_terminate_debuggee = true;
    capabilities.support_suspend_debuggee = false;

    protocol::InitializeResponse response;
    response.body = capabilities;
    send_response(request.seq, request.command, std::move(response));

    // Mark initialize consumed only after the response is enqueued; the
    // initialized event follows on the same ordered outbound path.
    initialize_request_handled_ = true;
    send(nlohmann::json(protocol::InitializedEvent{}));
}
} // namespace dap_dbgeng::service
