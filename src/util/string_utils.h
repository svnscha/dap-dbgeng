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

// Parses a DAP memory reference ("0x..." hex or plain decimal) into an address.
// WinDbg's backtick group separators (00007ff6`5cc7154e) are tolerated.
inline bool try_parse_memory_reference(const std::string &memory_reference, std::uint64_t &address)
{
    std::string value = memory_reference;
    int base = 10;
    if (value.size() >= 2 && (value[0] == '0') && (value[1] == 'x' || value[1] == 'X'))
    {
        value = value.substr(2);
        base = 16;
    }
    value.erase(std::remove(value.begin(), value.end(), '`'), value.end());
    if (value.empty())
    {
        return false;
    }
    try
    {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed, base);
        if (consumed != value.size())
        {
            return false;
        }
        address = static_cast<std::uint64_t>(parsed);
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
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
