#include "util/windbg_command_classifier.h"

namespace dap_dbgeng::util::windbg_command_classifier
{
namespace
{
// Lowercase ASCII copy for case-insensitive comparison.
std::string to_lower_ascii(const std::string &value)
{
    std::string result = value;
    for (char &character : result)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

const std::set<std::string> &execution_control_commands()
{
    static const std::set<std::string> commands = {"g",  "gh", "gn", "gc", "p",  "pa", "pc",
                                                   "ph", "t",  "ta", "tc", "th", "gu", ".breakin"};
    return commands;
}

// Trim leading/trailing whitespace.
std::string trim(const std::string &value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return std::string{};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
} // namespace

bool is_execution_control_command(const std::string &command)
{
    // Split on ';' (RemoveEmptyEntries + TrimEntries), then take the first whitespace-delimited token of each
    // segment.
    std::size_t start = 0;
    while (start <= command.size())
    {
        const std::size_t separator = command.find(';', start);
        const std::size_t end = separator == std::string::npos ? command.size() : separator;
        const std::string segment = trim(command.substr(start, end - start));
        if (!segment.empty())
        {
            const std::size_t token_end = segment.find_first_of(" \t");
            const std::string first_token = token_end == std::string::npos ? segment : segment.substr(0, token_end);
            if (execution_control_commands().contains(to_lower_ascii(first_token)))
            {
                return true;
            }
        }

        if (separator == std::string::npos)
        {
            break;
        }
        start = separator + 1;
    }

    return false;
}
} // namespace dap_dbgeng::util::windbg_command_classifier
