#include "debugger/debugger_session.h"

#include "debugger/debugger_session_internal.h"

namespace dap_dbgeng::debugger
{
namespace
{
// Read one symbol-group string field (name / value text / type name) into a
// trimmed std::string; empty when the engine reports nothing.
std::string read_symbol_name(IDebugSymbolGroup2 *group, ULONG index)
{
    std::vector<char> buffer(kSymbolBufferBytes);
    ULONG size = 0;
    if (SUCCEEDED(group->GetSymbolName(index, buffer.data(), static_cast<ULONG>(buffer.size()), &size)) && size != 0)
    {
        return buffer_to_trimmed_string(buffer, size);
    }
    return {};
}

std::string read_symbol_value(IDebugSymbolGroup2 *group, ULONG index)
{
    std::vector<char> buffer(kLocalValueBufferBytes);
    ULONG size = 0;
    if (SUCCEEDED(group->GetSymbolValueText(index, buffer.data(), static_cast<ULONG>(buffer.size()), &size)) &&
        size != 0)
    {
        return buffer_to_trimmed_string(buffer, size);
    }
    return {};
}

std::string read_symbol_type(IDebugSymbolGroup2 *group, ULONG index)
{
    std::vector<char> buffer(kSymbolBufferBytes);
    ULONG size = 0;
    if (SUCCEEDED(group->GetSymbolTypeName(index, buffer.data(), static_cast<ULONG>(buffer.size()), &size)) &&
        size != 0)
    {
        return buffer_to_trimmed_string(buffer, size);
    }
    return {};
}

// A pointer type (trailing '*') is expandable but we do not auto-follow it: that
// risks cycles (linked lists) and unbounded depth. Such nodes stay leaves.
bool type_is_pointer(const std::string &type)
{
    for (auto it = type.rbegin(); it != type.rend(); ++it)
    {
        if (*it == '*')
        {
            return true;
        }
        if (std::isspace(static_cast<unsigned char>(*it)) == 0)
        {
            return false;
        }
    }
    return false;
}

// Hops from a symbol to a top-level (unparented) symbol; top-level == 0.
int symbol_depth(const std::vector<DEBUG_SYMBOL_PARAMETERS> &params, ULONG index)
{
    int depth = 0;
    ULONG parent = params[index].ParentSymbol;
    while (parent != kDebugAnyId && parent < params.size())
    {
        ++depth;
        parent = params[parent].ParentSymbol;
    }
    return depth;
}
} // namespace

// ---------------------------------------------------------------------------
// Stack / vars
// ---------------------------------------------------------------------------
std::vector<stack_frame_info> debugger_session::get_stack_trace(int max_frames)
{
    return get_stack_trace_details(max_frames).frames;
}

int debugger_session::get_stack_trace_fetch_count(int total_frames, int max_frames)
{
    if (total_frames < 0)
    {
        throw std::out_of_range("totalFrames");
    }
    if (max_frames < 0)
    {
        throw std::out_of_range("maxFrames");
    }
    return std::min(total_frames, max_frames);
}

stack_trace_details debugger_session::get_stack_trace_details(int max_frames)
{
    throw_if_disposed();
    if (max_frames < 0)
    {
        throw std::out_of_range("maxFrames");
    }

    // Walk the full stack once (capped) so totalFrames reflects the real depth for
    // paging, then resolve each returned frame's name and source via the symbol APIs.
    constexpr ULONG kMaxStackDepth = 1024;
    std::vector<DEBUG_STACK_FRAME_EX> frames(kMaxStackDepth);
    ULONG filled = 0;
    check_hr(control_->GetStackTraceEx(0, 0, 0, frames.data(), static_cast<ULONG>(frames.size()), &filled),
             "Could not enumerate stack trace");
    frames.resize(filled);

    const int total_frames = static_cast<int>(filled);
    const int fetch_count = get_stack_trace_fetch_count(total_frames, max_frames);

    std::vector<stack_frame_info> result;
    result.reserve(fetch_count);
    for (int i = 0; i < fetch_count; ++i)
    {
        const DEBUG_STACK_FRAME_EX &frame = frames[i];
        stack_frame_info info;
        info.frame_number = frame.FrameNumber;
        info.instruction_offset = frame.InstructionOffset;
        info.name = try_get_frame_name(frame.InstructionOffset);
        info.source = try_get_frame_source(frame.InstructionOffset);
        result.push_back(std::move(info));
    }

    return stack_trace_details{std::move(result), total_frames};
}

std::vector<named_value_info> debugger_session::get_locals(std::uint32_t frame_number)
{
    throw_if_disposed();

    // Set the scope to the requested frame, then read locals + arguments from the
    // engine's scope symbol group (the structured path WinDbg's Locals window
    // uses; `dv` text omits variable names in kernel mode).
    execute_command_with_output(fmt::format(".frame {}", frame_number), /*suppress_output_events=*/true);

    IDebugSymbolGroup2 *group = nullptr;
    {
        PDEBUG_SYMBOL_GROUP base_group = nullptr;
        const HRESULT hr = symbols_->GetScopeSymbolGroup(kDebugScopeGroupAll, nullptr, &base_group);
        if (FAILED(hr) || base_group == nullptr)
        {
            return {};
        }
        // GetScopeSymbolGroup returns IDebugSymbolGroup; QI for the 2 variant.
        const HRESULT qi = base_group->QueryInterface(__uuidof(IDebugSymbolGroup2), reinterpret_cast<PVOID *>(&group));
        base_group->Release();
        if (FAILED(qi) || group == nullptr)
        {
            return {};
        }
    }

    std::vector<named_value_info> result;
    ULONG count = 0;
    if (SUCCEEDED(group->GetNumberSymbols(&count)))
    {
        result.reserve(count);
        std::vector<char> buffer(kLocalValueBufferBytes);
        for (ULONG index = 0; index < count; ++index)
        {
            ULONG name_size = 0;
            if (FAILED(group->GetSymbolName(index, buffer.data(), static_cast<ULONG>(buffer.size()), &name_size)) ||
                name_size == 0)
            {
                continue;
            }
            const std::string name = buffer_to_trimmed_string(buffer, name_size);
            if (name.empty())
            {
                continue;
            }

            ULONG value_size = 0;
            std::string value;
            if (SUCCEEDED(
                    group->GetSymbolValueText(index, buffer.data(), static_cast<ULONG>(buffer.size()), &value_size)) &&
                value_size != 0)
            {
                value = buffer_to_trimmed_string(buffer, value_size);
            }

            result.push_back(named_value_info{name, normalize_value_text(value)});
        }
    }

    group->Release();
    return result;
}

std::vector<variable_node> debugger_session::get_locals_tree(std::uint32_t frame_number)
{
    throw_if_disposed();

    // Same scope symbol group get_locals reads, but here we expand aggregate
    // members in place so structs/classes become a tree. The group is only live
    // for this call; we snapshot the whole (bounded) tree before releasing it, so
    // later variables requests never need the group again.
    execute_command_with_output(fmt::format(".frame {}", frame_number), /*suppress_output_events=*/true);

    IDebugSymbolGroup2 *group = nullptr;
    {
        PDEBUG_SYMBOL_GROUP base_group = nullptr;
        const HRESULT hr = symbols_->GetScopeSymbolGroup(kDebugScopeGroupAll, nullptr, &base_group);
        if (FAILED(hr) || base_group == nullptr)
        {
            return {};
        }
        const HRESULT qi = base_group->QueryInterface(__uuidof(IDebugSymbolGroup2), reinterpret_cast<PVOID *>(&group));
        base_group->Release();
        if (FAILED(qi) || group == nullptr)
        {
            return {};
        }
    }

    // Phase 1: expand value aggregates, bounded by depth and a total-node budget.
    // ExpandSymbol inserts children right after the parent and shifts every later
    // index, so we re-query parameters from scratch after each expansion and act
    // on one symbol per scan. The EXPANDED flag stops us re-expanding a node;
    // pointers are skipped (no auto-follow). The loop terminates because each
    // successful expansion sets a flag, shrinking the eligible set.
    for (;;)
    {
        ULONG count = 0;
        if (FAILED(group->GetNumberSymbols(&count)) || count == 0 || count >= kMaxExpandedNodes)
        {
            break;
        }
        std::vector<DEBUG_SYMBOL_PARAMETERS> params(count);
        if (FAILED(group->GetSymbolParameters(0, count, params.data())))
        {
            break;
        }

        bool expanded_one = false;
        for (ULONG i = 0; i < count; ++i)
        {
            const DEBUG_SYMBOL_PARAMETERS &p = params[i];
            if ((p.Flags & kDebugSymbolExpanded) != 0 || p.SubElements == 0)
            {
                continue;
            }
            if (symbol_depth(params, i) >= kMaxExpansionDepth || type_is_pointer(read_symbol_type(group, i)))
            {
                continue;
            }
            if (SUCCEEDED(group->ExpandSymbol(i, TRUE)))
            {
                expanded_one = true;
                break;
            }
        }
        if (!expanded_one)
        {
            break;
        }
    }

    // Phase 2: read the now-stable flat list and rebuild the tree via ParentSymbol.
    std::vector<variable_node> roots;
    ULONG count = 0;
    if (SUCCEEDED(group->GetNumberSymbols(&count)) && count != 0)
    {
        std::vector<DEBUG_SYMBOL_PARAMETERS> params(count);
        if (SUCCEEDED(group->GetSymbolParameters(0, count, params.data())))
        {
            std::vector<variable_node> nodes(count);
            for (ULONG i = 0; i < count; ++i)
            {
                nodes[i].name = read_symbol_name(group, i);
                nodes[i].value = normalize_value_text(read_symbol_value(group, i));
                nodes[i].type = read_symbol_type(group, i);
                // Expandable but not inlined (depth/budget cap or pointer): leave
                // children empty so the caller can still flag it if it wants.
                nodes[i].is_expandable = params[i].SubElements != 0 && (params[i].Flags & kDebugSymbolExpanded) == 0;
            }

            // Children always have a higher index than their parent, so linking
            // high-to-low moves a fully built child subtree into its parent before
            // the parent itself is moved. Inserting at the front preserves order.
            for (ULONG idx = count; idx-- > 0;)
            {
                const ULONG parent = params[idx].ParentSymbol;
                if (parent != kDebugAnyId && parent < count)
                {
                    nodes[parent].children.insert(nodes[parent].children.begin(), std::move(nodes[idx]));
                }
            }
            for (ULONG i = 0; i < count; ++i)
            {
                if (params[i].ParentSymbol == kDebugAnyId)
                {
                    roots.push_back(std::move(nodes[i]));
                }
            }
        }
    }

    group->Release();
    return roots;
}

std::vector<named_value_info> debugger_session::get_registers(std::uint32_t frame_number)
{
    throw_if_disposed();

    // Point the register source at the requested frame, then read the register set
    // structurally (the equivalent of `.frame N; r`). A frame that cannot be scoped
    // has no register view to report.
    if (FAILED(symbols_->SetScopeFrameByIndex(frame_number)))
    {
        return {};
    }

    ULONG count = 0;
    if (registers_ == nullptr || FAILED(registers_->GetNumberRegisters(&count)) || count == 0)
    {
        return {};
    }

    // Read each register individually: a batch read fails atomically if any single
    // register (e.g. an absent AVX register) can't be read. Skip sub-registers
    // (al/ax/eax alias rax) and non-integer registers so the set mirrors the
    // general-purpose register view rather than every alias and vector lane.
    std::vector<named_value_info> result;
    std::vector<char> name_buffer(kRegisterNameBufferBytes);
    for (ULONG index = 0; index < count; ++index)
    {
        ULONG name_size = 0;
        DEBUG_REGISTER_DESCRIPTION description{};
        if (FAILED(registers_->GetDescription(index, name_buffer.data(), static_cast<ULONG>(name_buffer.size()),
                                              &name_size, &description)))
        {
            continue;
        }
        if ((description.Flags & DEBUG_REGISTER_SUB_REGISTER) != 0)
        {
            continue;
        }
        if (description.Type != DEBUG_VALUE_INT8 && description.Type != DEBUG_VALUE_INT16 &&
            description.Type != DEBUG_VALUE_INT32 && description.Type != DEBUG_VALUE_INT64)
        {
            continue;
        }

        DEBUG_VALUE value{};
        if (FAILED(registers_->GetValues2(DEBUG_REGSRC_FRAME, 1, &index, 0, &value)) &&
            FAILED(registers_->GetValues(1, &index, 0, &value)))
        {
            continue;
        }

        const std::string name = buffer_to_trimmed_string(name_buffer, name_size);
        if (!name.empty())
        {
            result.push_back(named_value_info{name, format_register_value(value)});
        }
    }
    return result;
}

std::optional<source_location> debugger_session::try_get_frame_source(std::uint64_t instruction_offset)
{
    throw_if_disposed();

    std::vector<char> buffer(kSymbolBufferBytes);
    ULONG line = 0;
    ULONG file_size = 0;
    ULONG64 displacement = 0;
    const HRESULT hr = symbols_->GetLineByOffset(instruction_offset, &line, buffer.data(),
                                                 static_cast<ULONG>(buffer.size()), &file_size, &displacement);
    if (FAILED(hr) || line == 0 || file_size == 0)
    {
        return std::nullopt;
    }

    std::size_t length = std::min(static_cast<std::size_t>(file_size), buffer.size());
    if (length > 0 && buffer[length - 1] == '\0')
    {
        --length;
    }
    if (length == 0)
    {
        return std::nullopt;
    }

    std::string path(buffer.data(), length);
    if (trim_both(path).empty())
    {
        return std::nullopt;
    }
    return source_location{path, static_cast<int>(line)};
}

// ---------------------------------------------------------------------------
// Memory / disassembly
// ---------------------------------------------------------------------------
std::optional<std::string> debugger_session::try_get_frame_name(std::uint64_t instruction_offset)
{
    std::vector<char> buffer(kSymbolBufferBytes);
    ULONG name_size = 0;
    ULONG64 displacement = 0;
    const HRESULT hr = symbols_->GetNameByOffset(instruction_offset, buffer.data(), static_cast<ULONG>(buffer.size()),
                                                 &name_size, &displacement);
    if (FAILED(hr) || name_size == 0)
    {
        return std::nullopt;
    }
    std::size_t length = std::min(static_cast<std::size_t>(name_size), buffer.size());
    if (length > 0 && buffer[length - 1] == '\0')
    {
        --length;
    }
    std::string name = trim_both(std::string(buffer.data(), length));
    if (name.empty())
    {
        return std::nullopt;
    }
    return displacement == 0 ? name : fmt::format("{}+0x{:X}", name, displacement);
}

bool debugger_session::try_get_symbol_start(std::uint64_t instruction_offset, std::uint64_t &symbol_start)
{
    symbol_start = 0;

    std::vector<char> buffer(kSymbolBufferBytes);
    ULONG name_size = 0;
    ULONG64 displacement = 0;
    const HRESULT hr = symbols_->GetNameByOffset(instruction_offset, buffer.data(), static_cast<ULONG>(buffer.size()),
                                                 &name_size, &displacement);
    if (FAILED(hr) || name_size == 0)
    {
        return false;
    }
    std::size_t length = std::min(static_cast<std::size_t>(name_size), buffer.size());
    if (length > 0 && buffer[length - 1] == '\0')
    {
        --length;
    }
    const std::string name = trim_both(std::string(buffer.data(), length));
    if (name.empty())
    {
        return false;
    }

    std::uint64_t effective_displacement = displacement;
    if (effective_displacement == 0)
    {
        std::uint64_t inline_displacement = 0;
        if (try_get_inline_symbol_displacement(name, inline_displacement))
        {
            effective_displacement = inline_displacement;
        }
    }

    if (effective_displacement > instruction_offset)
    {
        return false; // would underflow
    }
    symbol_start = instruction_offset - effective_displacement;
    return true;
}
} // namespace dap_dbgeng::debugger
