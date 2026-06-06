#include "util/dap_message_inspector.h"

namespace dap_dbgeng::util::dap_message_inspector
{
std::string get_command(const protocol::Request &request)
{
    if (!request.command.empty() && request.command.find_first_not_of(" \t\r\n") != std::string::npos)
    {
        return request.command;
    }

    const nlohmann::json serialized = request;
    return try_get_command(serialized).value_or(std::string{});
}

std::optional<std::string> try_get_command(const nlohmann::json &message)
{
    if (!message.is_object())
    {
        return std::nullopt;
    }

    const auto command = message.find("command");
    if (command == message.end() || !command->is_string())
    {
        return std::nullopt;
    }

    return command->get<std::string>();
}

std::optional<int> try_get_sequence(const nlohmann::json &message)
{
    if (!message.is_object())
    {
        return std::nullopt;
    }

    const auto sequence = message.find("seq");
    if (sequence == message.end() || !sequence->is_number_integer())
    {
        return std::nullopt;
    }

    return sequence->get<int>();
}
} // namespace dap_dbgeng::util::dap_message_inspector
