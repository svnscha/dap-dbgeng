#pragma once

namespace dap_dbgeng::util
{
// True when a string is empty or only ASCII whitespace.
inline bool is_blank(const std::string &value)
{
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

// True when an optional string is unset, empty, or only ASCII whitespace.
inline bool is_blank(const std::optional<std::string> &value)
{
    return !value.has_value() || is_blank(*value);
}

// Trim trailing ASCII whitespace.
inline std::string trim_end(const std::string &value)
{
    std::size_t end = value.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
        --end;
    }
    return value.substr(0, end);
}

// Trim leading and trailing ASCII whitespace.
inline std::string trim_both(const std::string &value)
{
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
    {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
        --end;
    }
    return value.substr(begin, end - begin);
}
} // namespace dap_dbgeng::util
