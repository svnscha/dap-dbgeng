#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_variables_request(const protocol::VariablesRequest &request)
{
    const int variables_reference = request.arguments.variables_reference;
    const std::optional<protocol::VariablesArgumentsFilter> filter = request.arguments.filter;
    const auto clamp = [](const std::optional<std::int64_t> &value) -> int {
        return (value.has_value() && *value > 0 && *value <= std::numeric_limits<int>::max()) ? static_cast<int>(*value)
                                                                                              : 0;
    };
    const int start = clamp(request.arguments.start);
    const int count = clamp(request.arguments.count);

    if (variables_reference <= 0)
    {
        send_error_response(request.seq, request.command,
                            "The variables request requires a positive variablesReference.");
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

    // Indexed filter on a named container yields an empty array; a null or
    // Named filter returns the container's variables.
    static const std::vector<protocol::Variable> empty;
    const std::vector<protocol::Variable> &variables =
        (filter == protocol::VariablesArgumentsFilter::Indexed) ? empty : container_it->second.variables;

    protocol::VariablesResponse response;
    for (std::size_t index = static_cast<std::size_t>(start); index < variables.size(); ++index)
    {
        if (count > 0 && response.body.variables.size() >= static_cast<std::size_t>(count))
        {
            break;
        }
        response.body.variables.push_back(variables[index]);
    }
    send_response(request.seq, request.command, std::move(response));
}
} // namespace dap_dbgeng::service
