#include "service/dap_server.h"

#include "util/string_utils.h"

namespace dap_dbgeng::service
{
void dap_server::handle_set_variable_request(const protocol::SetVariableRequest &request)
{
    const int variables_reference = request.arguments.variables_reference;
    const std::string &name = request.arguments.name;
    const std::string &value = request.arguments.value;

    if (variables_reference <= 0)
    {
        send_error_response(request.seq, request.command,
                            "The setVariable request requires a positive variablesReference.");
        return;
    }
    if (util::is_blank(name))
    {
        send_error_response(request.seq, request.command,
                            "The setVariable request requires a non-empty variable name.");
        return;
    }
    if (util::is_blank(value))
    {
        send_error_response(request.seq, request.command,
                            "The setVariable request requires a non-empty value expression.");
        return;
    }
    if (is_execution_running_.load())
    {
        send_error_response(request.seq, request.command,
                            "The debuggee is currently running. Wait until execution stops before changing variables.");
        return;
    }

    const auto container_it = variables_by_reference_.find(variables_reference);
    if (container_it == variables_by_reference_.end())
    {
        send_error_response(request.seq, request.command,
                            fmt::format("Unknown variablesReference '{}'. Request scopes again for the current stop.",
                                        variables_reference));
        return;
    }

    variable_container_context container = container_it->second;

    // A struct-field container assigns through the symbol group by the field's full
    // access path (e.g. "t.origin.x"), which the engine resolves and writes with
    // type awareness - distinct from the top-level locals/registers path below.
    if (container.kind == variable_container_kind::structure)
    {
        handle_set_struct_field(request, container, variables_reference, name, value);
        return;
    }

    debugger::debugger_session &session = require_debugger_session();

    try
    {
        const auto build_commands = [&](variable_container_kind kind, std::uint32_t frame_number) {
            std::vector<std::string> commands;
            if (kind == variable_container_kind::locals)
            {
                commands.push_back(fmt::format(".frame {};?? {}={}", frame_number, name, value));
                commands.push_back(fmt::format(".frame {};? @@c++({}={})", frame_number, name, value));
                commands.push_back(fmt::format(".frame {};?? $!{}={}", frame_number, name, value));
                commands.push_back(fmt::format(".frame {};? @@c++($!{}={})", frame_number, name, value));
            }
            else
            {
                commands.push_back(fmt::format("r {}={}", name, value));
            }
            return commands;
        };

        const auto read_variables = [&](debugger::debugger_session &s,
                                        const variable_container_context &c) -> std::vector<protocol::Variable> {
            const std::vector<debugger::named_value_info> values = c.kind == variable_container_kind::locals
                                                                       ? s.get_locals(c.frame_number)
                                                                       : s.get_registers(c.frame_number);
            std::vector<protocol::Variable> result;
            result.reserve(values.size());
            for (const auto &v : values)
            {
                result.push_back(create_variable(v));
            }
            return result;
        };

        const auto execute_set = [&](debugger::debugger_session &s, const variable_container_context &c) {
            const std::vector<std::string> commands = build_commands(c.kind, c.frame_number);
            std::optional<std::string> last_failure;
            for (const std::string &command : commands)
            {
                try
                {
                    s.execute_command_with_output(command, /*suppress_output_events=*/true);
                    return;
                }
                catch (const std::exception &exception)
                {
                    if (c.kind != variable_container_kind::locals)
                    {
                        throw;
                    }
                    // Only swallow "could not execute ... HRESULT=0x" failures to try the next candidate.
                    const std::string message = exception.what();
                    const bool is_invalid_assignment =
                        message.find(fmt::format("Could not execute debugger command '{}'", command)) !=
                            std::string::npos &&
                        message.find("HRESULT=0x") != std::string::npos;
                    if (!is_invalid_assignment)
                    {
                        throw;
                    }
                    last_failure = message;
                }
            }
            throw std::runtime_error(last_failure.value_or("No setVariable command candidates were generated."));
        };

        // set + refresh on the dispatcher thread, restoring the original thread.
        std::pair<variable_container_context, protocol::Variable> assignment = dispatcher_.invoke([&]() {
            const std::uint32_t original_thread_id = session.get_current_thread_id();
            try
            {
                session.set_current_thread(static_cast<std::uint32_t>(container.thread_id));
                execute_set(session, container);

                std::vector<protocol::Variable> refreshed = read_variables(session, container);
                std::optional<protocol::Variable> updated;
                for (const auto &variable : refreshed)
                {
                    if (variable.name == name)
                    {
                        updated = variable;
                        break;
                    }
                }
                if (!updated.has_value())
                {
                    throw std::runtime_error(fmt::format("Variable '{}' was not found after assignment.", name));
                }
                variable_container_context updated_container = container;
                updated_container.variables = std::move(refreshed);
                session.set_current_thread(original_thread_id);
                return std::make_pair(std::move(updated_container), *updated);
            }
            catch (...)
            {
                session.set_current_thread(original_thread_id);
                throw;
            }
        });

        variables_by_reference_[variables_reference] = assignment.first;

        protocol::SetVariableResponse response;
        const protocol::Variable &variable = assignment.second;
        response.body.value = variable.value;
        response.body.type = variable.type;
        response.body.variables_reference =
            variable.variables_reference > 0 ? std::optional<int>(variable.variables_reference) : std::nullopt;
        response.body.named_variables = variable.named_variables;
        response.body.indexed_variables = variable.indexed_variables;
        response.body.memory_reference = variable.memory_reference;
        response.body.value_location_reference = variable.value_location_reference;
        send_response(request.seq, request.command, std::move(response));
    }
    catch (const std::exception &exception)
    {
        send_error_response(request.seq, request.command,
                            util::debugger_session_dispatcher::unwrap_failure_message(exception, exception.what()));
    }
}

void dap_server::handle_set_struct_field(const protocol::SetVariableRequest &request,
                                         const variable_container_context &container, int variables_reference,
                                         const std::string &name, const std::string &value)
{
    // The cached child carries the field's full access path in evaluate_name; that
    // is what the engine resolves and writes (e.g. "t.origin.x").
    const protocol::Variable *child = nullptr;
    for (const auto &variable : container.variables)
    {
        if (variable.name == name)
        {
            child = &variable;
            break;
        }
    }
    if (child == nullptr)
    {
        send_error_response(request.seq, request.command,
                            fmt::format("Variable '{}' was not found in this struct.", name));
        return;
    }
    const std::string expression = child->evaluate_name.value_or(name);

    try
    {
        debugger::debugger_session &session = require_debugger_session();
        // Write + read-back on the dispatcher thread, restoring the original thread.
        std::pair<variable_container_context, protocol::Variable> assignment = dispatcher_.invoke([&]() {
            const std::uint32_t original_thread_id = session.get_current_thread_id();
            try
            {
                session.set_current_thread(static_cast<std::uint32_t>(container.thread_id));
                const debugger::variable_node updated =
                    session.set_local_value(container.frame_number, expression, value);

                // Refresh only the edited child in the cached container; its
                // variablesReference (and any child container) stays valid.
                variable_container_context updated_container = container;
                protocol::Variable result;
                for (auto &variable : updated_container.variables)
                {
                    if (variable.name == name)
                    {
                        variable.value = updated.value;
                        if (!updated.type.empty())
                        {
                            variable.type = updated.type;
                        }
                        result = variable;
                        break;
                    }
                }
                session.set_current_thread(original_thread_id);
                return std::make_pair(std::move(updated_container), result);
            }
            catch (...)
            {
                session.set_current_thread(original_thread_id);
                throw;
            }
        });

        variables_by_reference_[variables_reference] = assignment.first;

        protocol::SetVariableResponse response;
        const protocol::Variable &variable = assignment.second;
        response.body.value = variable.value;
        response.body.type = variable.type;
        response.body.variables_reference =
            variable.variables_reference > 0 ? std::optional<int>(variable.variables_reference) : std::nullopt;
        response.body.named_variables = variable.named_variables;
        response.body.indexed_variables = variable.indexed_variables;
        send_response(request.seq, request.command, std::move(response));
    }
    catch (const std::exception &exception)
    {
        send_error_response(request.seq, request.command,
                            util::debugger_session_dispatcher::unwrap_failure_message(exception, exception.what()));
    }
}
} // namespace dap_dbgeng::service
