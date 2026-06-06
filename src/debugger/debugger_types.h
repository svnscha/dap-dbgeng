#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dap_dbgeng::debugger
{
// A resolved source position (file path + 1-based line).
struct source_location
{
    std::string path;
    int line = 0;
};

// A single disassembled instruction.
struct disassembled_instruction_info
{
    std::uint64_t address = 0;
    std::string instruction;
    std::optional<std::string> instruction_bytes;
    std::optional<std::string> symbol;
    std::optional<source_location> source;
    bool is_invalid = false;
};

// A name/value pair for scopes and variables.
struct named_value_info
{
    std::string name;
    std::string value;
};

// A single stack frame.
struct stack_frame_info
{
    std::uint32_t frame_number = 0;
    std::uint64_t instruction_offset = 0;
    std::optional<std::string> name;
    std::optional<source_location> source;
};

// A stack trace plus the total frame count for paging.
struct stack_trace_details
{
    std::vector<stack_frame_info> frames;
    int total_frames = 0;
};

// A requested source breakpoint (line + optional condition).
struct source_breakpoint_spec
{
    int line = 0;
    std::optional<std::string> condition;
};

// The result of configuring a single source breakpoint.
struct source_breakpoint_result
{
    std::optional<int> id;
    int line = 0;
    bool verified = false;
    std::optional<std::string> message;
};

// A breakpoint's engine id, whether it is resolved (bound to an address), and
// its offset.
struct breakpoint_state
{
    int id = 0;
    bool resolved = false;
    std::uint64_t offset = 0;
};
} // namespace dap_dbgeng::debugger
