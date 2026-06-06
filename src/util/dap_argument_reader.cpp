#include "util/dap_argument_reader.h"

namespace dap_dbgeng::util::dap_argument_reader
{
namespace
{
// Quote a single command-line token using the Windows CommandLineToArgv rules
// (backslash/quote escaping).
std::string quote_command_line_token(const std::string &value)
{
    if (value.empty())
    {
        return "\"\"";
    }

    if (value.find_first_of(" \t\"") == std::string::npos)
    {
        return value;
    }

    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    int backslash_count = 0;

    for (const char character : value)
    {
        if (character == '\\')
        {
            ++backslash_count;
            continue;
        }

        if (character == '"')
        {
            result.append(static_cast<std::size_t>(backslash_count) * 2 + 1, '\\');
            result.push_back(character);
            backslash_count = 0;
            continue;
        }

        if (backslash_count > 0)
        {
            result.append(static_cast<std::size_t>(backslash_count), '\\');
            backslash_count = 0;
        }

        result.push_back(character);
    }

    if (backslash_count > 0)
    {
        result.append(static_cast<std::size_t>(backslash_count) * 2, '\\');
    }

    result.push_back('"');
    return result;
}

// Find a property on an object, returning end() unless `arguments` is an object.
nlohmann::json::const_iterator find_property(const nlohmann::json &arguments, const std::string &key)
{
    if (!arguments.is_object())
    {
        return arguments.end();
    }
    return arguments.find(key);
}
} // namespace

nlohmann::json get_arguments(const nlohmann::json &request)
{
    if (!request.is_object())
    {
        return nlohmann::json(nullptr);
    }

    const auto arguments = request.find("arguments");
    if (arguments == request.end() || arguments->is_null())
    {
        return nlohmann::json(nullptr);
    }

    return *arguments;
}

std::optional<std::string> try_get_string(const nlohmann::json &arguments, const std::string &key)
{
    const auto value = find_property(arguments, key);
    if (value == arguments.end() || !value->is_string())
    {
        return std::nullopt;
    }
    return value->get<std::string>();
}

std::optional<int> try_get_int32(const nlohmann::json &arguments, const std::string &key)
{
    const auto value = find_property(arguments, key);
    if (value == arguments.end())
    {
        return std::nullopt;
    }

    if (value->is_number_integer())
    {
        return value->get<int>();
    }

    if (value->is_string())
    {
        try
        {
            const std::string &text = value->get_ref<const std::string &>();
            std::size_t consumed = 0;
            const int parsed = std::stoi(text, &consumed);
            if (consumed == text.size())
            {
                return parsed;
            }
        }
        catch (const std::exception &)
        {
        }
    }

    return std::nullopt;
}

std::optional<bool> try_get_boolean(const nlohmann::json &arguments, const std::string &key)
{
    const auto value = find_property(arguments, key);
    if (value == arguments.end())
    {
        return std::nullopt;
    }

    if (value->is_boolean())
    {
        return value->get<bool>();
    }

    if (value->is_string())
    {
        const std::string &text = value->get_ref<const std::string &>();
        if (text == "true" || text == "True")
        {
            return true;
        }
        if (text == "false" || text == "False")
        {
            return false;
        }
    }

    return std::nullopt;
}

std::optional<std::vector<std::string>> try_get_string_list(const nlohmann::json &arguments, const std::string &key)
{
    const auto value = find_property(arguments, key);
    if (value == arguments.end())
    {
        return std::nullopt;
    }

    if (value->is_string())
    {
        return std::vector<std::string>{value->get<std::string>()};
    }

    if (!value->is_array())
    {
        return std::nullopt;
    }

    std::vector<std::string> result;
    for (const auto &item : *value)
    {
        if (item.is_string())
        {
            result.push_back(item.get<std::string>());
        }
    }
    return result;
}

std::optional<std::string> try_get_command_line_arguments(const nlohmann::json &arguments)
{
    if (!arguments.is_object())
    {
        return std::nullopt;
    }

    const auto value = arguments.find("args");
    if (value == arguments.end())
    {
        return std::nullopt;
    }

    if (value->is_string())
    {
        return value->get<std::string>();
    }

    if (!value->is_array())
    {
        return std::nullopt;
    }

    std::vector<std::string> tokens;
    for (const auto &item : *value)
    {
        tokens.push_back(item.is_string() ? quote_command_line_token(item.get<std::string>()) : item.dump());
    }

    if (tokens.empty())
    {
        return std::nullopt;
    }

    std::string joined;
    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        if (index != 0)
        {
            joined.push_back(' ');
        }
        joined += tokens[index];
    }
    return joined;
}

std::string resolve_working_directory(const nlohmann::json &arguments)
{
    const std::optional<std::string> working_directory = try_get_string(arguments, "workingDir");
    if (!working_directory.has_value())
    {
        return std::string{};
    }

    // Treat a blank value as unset.
    const std::string &value = *working_directory;
    if (value.find_first_not_of(" \t\r\n") == std::string::npos)
    {
        return std::string{};
    }

    return value;
}
} // namespace dap_dbgeng::util::dap_argument_reader
