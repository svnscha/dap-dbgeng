#pragma once

#include "debugger/debugger_types.h"

// Free helpers and engine constants shared by the debugger_session translation
// units. Defined inline so every unit that needs one gets the same definition.
namespace dap_dbgeng::debugger
{
// Fixed sizes for engine string outputs. dbgeng truncates into the supplied
// buffer and reports the size it needed; these comfortably hold symbol names,
// file paths, register names, and a single instruction's disassembly. A value
// longer than its buffer only affects display text, never control flow.
constexpr std::size_t kSymbolBufferBytes = 1024;
constexpr std::size_t kLocalValueBufferBytes = 512;
constexpr std::size_t kRegisterNameBufferBytes = 128;
constexpr std::size_t kDisassemblyLineBytes = 256;

// Throw a runtime_error carrying the failing HRESULT
// ("{message}. HRESULT=0x{hr:X8}").
inline void check_hr(HRESULT hr, const std::string &message)
{
    if (FAILED(hr))
    {
        throw std::runtime_error(fmt::format("{}. HRESULT=0x{:08X}", message, static_cast<std::uint32_t>(hr)));
    }
}

// dbgeng's WaitForEvent flag value (DEBUG_INTERRUPT_ACTIVE is 0; the wait flags
// default is 0). Kept separate for clarity.
constexpr ULONG kWaitDefaultFlags = DEBUG_WAIT_DEFAULT;

// A breakpoint is deferred (unresolved) when its flags include this bit.
constexpr ULONG kDebugBreakpointDeferred = 0x00000002;

// DEBUG_SCOPE_GROUP_ALL == arguments | locals.
constexpr ULONG kDebugScopeGroupAll = DEBUG_SCOPE_GROUP_ALL;

// Engine-state argument values are DEBUG_STATUS_* codes.
constexpr ULONG64 kStatusBreak = DEBUG_STATUS_BREAK;
constexpr ULONG64 kStatusNoDebuggee = DEBUG_STATUS_NO_DEBUGGEE;

// Resolve the dbgeng directory: enginePath may be a directory or a dll path;
// either way return the directory holding dbgeng.dll.
inline std::string get_dbgeng_directory(const std::string &engine_path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory(engine_path, ec))
    {
        return engine_path;
    }

    fs::path p(engine_path);
    if (p.has_parent_path())
    {
        return p.parent_path().string();
    }

    throw std::invalid_argument("The debugger engine path must resolve to a directory.");
}

inline std::string build_command_line(const std::string &executable_path, const std::optional<std::string> &arguments)
{
    const std::string quoted =
        executable_path.find(' ') != std::string::npos ? fmt::format("\"{}\"", executable_path) : executable_path;
    if (!arguments || arguments->empty())
    {
        return quoted;
    }
    return fmt::format("{} {}", quoted, *arguments);
}

inline void throw_if_null_or_whitespace(const std::string &value, const char *what)
{
    bool blank = value.empty();
    if (!blank)
    {
        blank = std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    }
    if (blank)
    {
        throw std::invalid_argument(fmt::format("{} must not be null or whitespace.", what));
    }
}

inline std::string to_upper_invariant(const std::string &value)
{
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

// Path.GetFullPath equivalent for breakpoint normalization / matching.
inline std::string get_full_path(const std::string &path)
{
    std::error_code ec;
    std::filesystem::path full = std::filesystem::absolute(path, ec);
    if (ec)
    {
        return path;
    }
    return full.lexically_normal().string();
}

inline std::string trim_end(const std::string &value)
{
    std::size_t end = value.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return value.substr(0, end);
}

inline std::string trim_both(const std::string &value)
{
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
    {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

// Trim a trailing NUL the engine appends to a size-counted buffer, then trim
// surrounding whitespace.
inline std::string buffer_to_trimmed_string(const std::vector<char> &buffer, ULONG size)
{
    if (size == 0)
    {
        return {};
    }
    std::size_t length = std::min(static_cast<std::size_t>(size), buffer.size());
    if (length > 0 && buffer[length - 1] == '\0')
    {
        --length;
    }
    if (length == 0)
    {
        return {};
    }
    return trim_both(std::string(buffer.data(), length));
}

// Strip WinDbg's "0n" decimal display prefix (e.g. "0n13" -> "13", "0n-5" -> "-5",
// "0n+7" -> "+7"); other value formats (hex, struct text) pass through unchanged.
inline std::string normalize_value_text(const std::string &value)
{
    if (value.size() <= 2 || value[0] != '0' || (value[1] != 'n' && value[1] != 'N'))
    {
        return value;
    }
    std::size_t i = 2;
    if (value[i] == '+' || value[i] == '-')
    {
        ++i;
    }
    if (i >= value.size())
    {
        return value;
    }
    for (std::size_t j = i; j < value.size(); ++j)
    {
        if (std::isdigit(static_cast<unsigned char>(value[j])) == 0)
        {
            return value;
        }
    }
    return value.substr(2);
}

// Lowercase hex of raw instruction bytes (e.g. {0x48,0x8b,0xec} -> "488bec").
inline std::string format_hex_bytes(const std::vector<unsigned char> &bytes)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char b : bytes)
    {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

// Format a register's typed value as fixed-width lowercase hex, matching the columns
// WinDbg's `r` produces (width follows the register's integer size; other types show
// their low 64 bits).
inline std::string format_register_value(const DEBUG_VALUE &value)
{
    switch (value.Type)
    {
    case DEBUG_VALUE_INT8:
        return fmt::format("{:02x}", value.I8);
    case DEBUG_VALUE_INT16:
        return fmt::format("{:04x}", value.I16);
    case DEBUG_VALUE_INT32:
        return fmt::format("{:08x}", value.I32);
    case DEBUG_VALUE_INT64:
    default:
        return fmt::format("{:016x}", value.I64);
    }
}

// A single IDebugControl::Disassemble line is "address bytes mnemonic[ operands]".
// Drop the address and raw-bytes columns, leaving the mnemonic and operands.
inline std::string extract_instruction_text(const std::string &line)
{
    std::string s = line;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
    {
        s.pop_back();
    }
    if (const std::size_t newline = s.find_last_of('\n'); newline != std::string::npos)
    {
        s = s.substr(newline + 1);
    }

    std::size_t i = 0;
    const auto skip_spaces = [&] {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        {
            ++i;
        }
    };
    const auto skip_token = [&] {
        while (i < s.size() && s[i] != ' ' && s[i] != '\t')
        {
            ++i;
        }
    };
    skip_spaces();
    skip_token(); // address column
    skip_spaces();
    skip_token(); // raw-bytes column
    skip_spaces();

    std::string text = s.substr(i);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    {
        text.pop_back();
    }
    return text;
}

inline bool try_parse_breakpoint_id_after_token(const std::string &description, const std::string &token,
                                                std::uint32_t &breakpoint_id)
{
    breakpoint_id = 0;
    // Case-insensitive search for the token.
    const std::string upper_desc = to_upper_invariant(description);
    const std::string upper_token = to_upper_invariant(token);
    const std::size_t token_index = upper_desc.find(upper_token);
    if (token_index == std::string::npos)
    {
        return false;
    }

    std::size_t pos = token_index + token.size();
    while (pos < description.size() && std::isspace(static_cast<unsigned char>(description[pos])))
    {
        ++pos;
    }

    std::size_t digit_start = pos;
    while (pos < description.size() && std::isdigit(static_cast<unsigned char>(description[pos])))
    {
        ++pos;
    }

    if (pos == digit_start)
    {
        return false;
    }

    try
    {
        unsigned long parsed = std::stoul(description.substr(digit_start, pos - digit_start));
        breakpoint_id = static_cast<std::uint32_t>(parsed);
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

// Parse the inline "+0x..." displacement from a symbol name.
inline bool try_get_inline_symbol_displacement(const std::string &name, std::uint64_t &displacement)
{
    displacement = 0;
    const std::string upper = to_upper_invariant(name);
    const std::size_t sep = upper.rfind("+0X");
    if (sep == std::string::npos)
    {
        return false;
    }
    const std::string text = name.substr(sep + 3);
    if (text.empty())
    {
        return false;
    }
    try
    {
        std::size_t consumed = 0;
        std::uint64_t value = std::stoull(text, &consumed, 16);
        if (consumed != text.size())
        {
            return false;
        }
        displacement = value;
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

inline std::vector<disassembled_instruction_info> create_invalid_leading_instructions(std::uint64_t memory_address,
                                                                                      int count)
{
    std::vector<disassembled_instruction_info> instructions;
    if (count <= 0)
    {
        return instructions;
    }
    std::uint64_t start_address =
        memory_address >= static_cast<std::uint64_t>(count) ? memory_address - static_cast<std::uint64_t>(count) : 0;
    for (int index = 0; index < count; ++index)
    {
        disassembled_instruction_info info;
        info.address = start_address + static_cast<std::uint64_t>(index);
        info.instruction = "<invalid instruction>";
        info.is_invalid = true;
        instructions.push_back(std::move(info));
    }
    return instructions;
}
} // namespace dap_dbgeng::debugger
