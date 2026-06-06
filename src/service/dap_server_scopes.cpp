#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_scopes_request(const protocol::ScopesRequest &request)
{
    const int frame_id = request.arguments.frame_id;
    if (frame_id <= 0)
    {
        send_error_response(request.seq, request.command, "The scopes request requires a positive frameId.");
        return;
    }

    const auto context_it = frame_contexts_.find(frame_id);
    if (context_it == frame_contexts_.end())
    {
        send_error_response(
            request.seq, request.command,
            fmt::format("Unknown frame id '{}'. Request stackTrace again for the current stop.", frame_id));
        return;
    }
    const frame_context context = context_it->second;

    using named_values = std::vector<debugger::named_value_info>;
    std::pair<named_values, named_values> values = read_session_data_or_default<>(
        [this, &context](debugger::debugger_session &session) {
            named_values locals = dispatcher_.invoke([&]() { return session.get_locals(context.frame_number); });
            named_values registers = dispatcher_.invoke([&]() { return session.get_registers(context.frame_number); });
            return std::make_pair(std::move(locals), std::move(registers));
        },
        std::make_pair(named_values{}, named_values{}));

    const named_values &locals = values.first;
    const named_values &registers = values.second;

    std::vector<protocol::Variable> local_variables;
    local_variables.reserve(locals.size());
    for (const auto &value : locals)
    {
        local_variables.push_back(create_variable(value));
    }
    std::vector<protocol::Variable> register_variables;
    register_variables.reserve(registers.size());
    for (const auto &value : registers)
    {
        register_variables.push_back(create_variable(value));
    }

    protocol::Scope locals_scope =
        create_scope("Locals", protocol::ScopePresentationHint::Locals,
                     create_variables_reference(context, variable_container_kind::locals, local_variables),
                     static_cast<int>(locals.size()), context.source);
    protocol::Scope registers_scope =
        create_scope("Registers", protocol::ScopePresentationHint::Registers,
                     create_variables_reference(context, variable_container_kind::registers, register_variables),
                     static_cast<int>(registers.size()), std::nullopt);

    protocol::ScopesResponse response;
    response.body.scopes = {std::move(locals_scope), std::move(registers_scope)};
    send_response(request.seq, request.command, std::move(response));
}
} // namespace dap_dbgeng::service
