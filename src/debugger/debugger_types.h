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

// A local variable as a tree node, so structured values (structs, classes,
// nested aggregates) can be expanded. `children` is populated up to a depth cap
// during enumeration; `is_expandable` is set when the engine reports children we
// did not inline (depth/budget cap reached), so a client can still show the
// expand affordance. Leaf scalars have no children and is_expandable == false.
struct variable_node
{
    std::string name;
    std::string value;
    std::string type;
    bool is_expandable = false;
    // Absolute address and byte size when the symbol has one (0 when
    // enregistered or unknown); feeds memoryReference and data breakpoints.
    std::uint64_t address = 0;
    std::uint32_t size = 0;
    std::vector<variable_node> children;
};

// A loaded module as reported by the engine.
struct module_info
{
    std::string name;
    std::string image_path;
    std::uint64_t base = 0;
    std::uint32_t size = 0;
    std::string symbol_status;
};

// The most recent exception event (set by the engine callback while the
// debuggee is stopped on an exception).
struct last_exception_info
{
    std::uint32_t code = 0;
    std::uint64_t address = 0;
    bool first_chance = false;
};

// One requested data (hardware) breakpoint: an address, a byte size (1/2/4/8),
// and whether it also breaks on reads.
struct data_breakpoint_spec
{
    std::uint64_t address = 0;
    std::uint32_t size = 0;
    bool break_on_read = false;
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

// A running process as reported by the engine (locally or over a process server).
struct process_info
{
    std::uint32_t system_id = 0;
    std::string name;
    std::string description;
};
} // namespace dap_dbgeng::debugger
