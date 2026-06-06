#include "debugger/debugger_session.h"

#include "debugger/debugger_session_internal.h"

namespace dap_dbgeng::debugger
{
// ---------------------------------------------------------------------------
// Breakpoints
// ---------------------------------------------------------------------------
std::uint32_t debugger_session::set_breakpoint(const std::string &file_path, int line)
{
    throw_if_disposed();
    throw_if_null_or_whitespace(file_path, "filePath");
    if (line <= 0)
    {
        throw std::out_of_range("line");
    }

    const auto results = set_source_breakpoints(file_path, std::vector<int>{line});
    if (results.size() != 1 || !results.front().id)
    {
        throw std::runtime_error(fmt::format("Could not set breakpoint at '{}:{}'.", file_path, line));
    }
    return static_cast<std::uint32_t>(*results.front().id);
}

std::vector<source_breakpoint_result> debugger_session::set_source_breakpoints(const std::string &file_path,
                                                                               const std::vector<int> &lines)
{
    std::vector<source_breakpoint_spec> specs;
    specs.reserve(lines.size());
    for (int line : lines)
    {
        specs.push_back(source_breakpoint_spec{line, std::nullopt});
    }
    return set_source_breakpoints(file_path, specs);
}

std::vector<source_breakpoint_result> debugger_session::set_source_breakpoints(
    const std::string &file_path, const std::vector<source_breakpoint_spec> &breakpoints)
{
    throw_if_disposed();
    throw_if_null_or_whitespace(file_path, "filePath");

    const std::string normalized_path = get_full_path(file_path);

    // Spec equality is (line, condition ordinal). We dedupe and remember the
    // unique result for each spec by an explicit key.
    auto spec_key = [](const source_breakpoint_spec &s) {
        return fmt::format("{}\x1f{}", s.line, s.condition ? *s.condition : std::string("\x1e"));
    };

    std::map<std::string, source_breakpoint_result> configured_breakpoints;
    std::vector<int> configured_breakpoint_ids;

    execute_command_with_output(".lines -e", /*suppress_output_events=*/true);
    clear_source_breakpoints(normalized_path);

    if (breakpoints.empty())
    {
        return {};
    }

    // Distinct by (line, condition), only lines > 0, preserving first occurrence.
    std::vector<source_breakpoint_spec> distinct;
    {
        std::map<std::string, bool> seen;
        for (const auto &bp : breakpoints)
        {
            if (bp.line <= 0)
            {
                continue;
            }
            const std::string key = spec_key(bp);
            if (seen.find(key) == seen.end())
            {
                seen[key] = true;
                distinct.push_back(bp);
            }
        }
    }

    for (const auto &breakpoint : distinct)
    {
        const std::vector<int> before_ids = get_configured_breakpoint_ids();

        // CreateSourceBreakpointCommand: bu `path:line` (+ /w "cond" if set).
        const std::string source_location_token = fmt::format("`{}:{}`", normalized_path, breakpoint.line);
        std::string command;
        if (!breakpoint.condition || trim_both(*breakpoint.condition).empty())
        {
            command = fmt::format("bu {}", source_location_token);
        }
        else
        {
            std::string escaped;
            escaped.reserve(breakpoint.condition->size());
            for (char ch : *breakpoint.condition)
            {
                if (ch == '\\')
                {
                    escaped += "\\\\";
                }
                else if (ch == '"')
                {
                    escaped += "\\\"";
                }
                else
                {
                    escaped += ch;
                }
            }
            command = fmt::format("bu /w \"{}\" {}", escaped, source_location_token);
        }

        execute_command_with_output(command, /*suppress_output_events=*/true);
        const std::vector<int> after_ids = get_configured_breakpoint_ids();

        // newIds = after - before (set difference).
        std::vector<int> new_ids;
        {
            std::set<int> before_set(before_ids.begin(), before_ids.end());
            for (int id : after_ids)
            {
                if (before_set.find(id) == before_set.end())
                {
                    new_ids.push_back(id);
                }
            }
        }

        const std::string key = spec_key(breakpoint);
        if (new_ids.size() == 1)
        {
            configured_breakpoint_ids.push_back(new_ids[0]);
            source_breakpoint_result result;
            result.id = new_ids[0];
            result.line = breakpoint.line;
            result.verified = true;
            configured_breakpoints[key] = std::move(result);
        }
        else
        {
            source_breakpoint_result result;
            result.line = breakpoint.line;
            result.verified = false;
            result.message = fmt::format("Unable to resolve breakpoint at '{}:{}'.", normalized_path, breakpoint.line);
            configured_breakpoints[key] = std::move(result);
        }
    }

    source_breakpoint_ids_by_path_[to_upper_invariant(normalized_path)] = configured_breakpoint_ids;

    // Project the original requested order to results.
    std::vector<source_breakpoint_result> output;
    output.reserve(breakpoints.size());
    for (const auto &breakpoint : breakpoints)
    {
        if (breakpoint.line <= 0)
        {
            source_breakpoint_result result;
            result.line = breakpoint.line;
            result.verified = false;
            result.message = "Breakpoint lines must be greater than zero.";
            output.push_back(std::move(result));
            continue;
        }
        output.push_back(configured_breakpoints[spec_key(breakpoint)]);
    }
    return output;
}

std::vector<breakpoint_state> debugger_session::get_breakpoint_states()
{
    throw_if_disposed();

    ULONG count = 0;
    if (FAILED(control_->GetNumberBreakpoints(&count)))
    {
        return {};
    }

    std::vector<breakpoint_state> result;
    result.reserve(count);
    for (ULONG i = 0; i < count; ++i)
    {
        IDebugBreakpoint *bp = nullptr;
        if (FAILED(control_->GetBreakpointByIndex(i, &bp)) || bp == nullptr)
        {
            continue;
        }
        // GetBreakpointByIndex returns an engine-owned (borrowed) pointer; do
        // not Release it.
        ULONG id = 0;
        if (FAILED(bp->GetId(&id)))
        {
            continue;
        }
        ULONG flags = 0;
        bp->GetFlags(&flags);
        ULONG64 offset = 0;
        bp->GetOffset(&offset);

        breakpoint_state state;
        state.id = static_cast<int>(id);
        state.resolved = (flags & kDebugBreakpointDeferred) == 0;
        state.offset = offset;
        result.push_back(state);
    }
    return result;
}

std::vector<int> debugger_session::get_configured_breakpoint_ids()
{
    std::vector<int> ids;
    for (const auto &state : get_breakpoint_states())
    {
        ids.push_back(state.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void debugger_session::clear_all_breakpoints()
{
    throw_if_disposed();
    execute_command_with_output("bc *", /*suppress_output_events=*/true);
}

void debugger_session::clear_source_breakpoints(const std::string &normalized_path)
{
    const std::string key = to_upper_invariant(normalized_path);
    auto it = source_breakpoint_ids_by_path_.find(key);
    if (it == source_breakpoint_ids_by_path_.end())
    {
        return;
    }
    const std::vector<int> ids = it->second;
    source_breakpoint_ids_by_path_.erase(it);
    for (int id : ids)
    {
        execute_command_with_output(fmt::format("bc {}", id), /*suppress_output_events=*/true);
    }
}

// ---------------------------------------------------------------------------
// Last-event breakpoint id parsing
// ---------------------------------------------------------------------------
std::uint32_t debugger_session::get_last_breakpoint_id()
{
    ULONG type = 0;
    ULONG process_id = 0;
    ULONG thread_id = 0;
    ULONG description_used = 0;
    std::vector<char> description(kSymbolBufferBytes);
    const HRESULT hr =
        control_->GetLastEventInformation(&type, &process_id, &thread_id, nullptr, 0, nullptr, description.data(),
                                          static_cast<ULONG>(description.size()), &description_used);
    if (FAILED(hr))
    {
        return 0;
    }
    std::string desc;
    if (description_used > 0)
    {
        std::size_t length = std::min(static_cast<std::size_t>(description_used), description.size());
        if (length > 0 && description[length - 1] == '\0')
        {
            --length;
        }
        desc.assign(description.data(), length);
    }

    std::uint32_t breakpoint_id = 0;
    if (try_parse_breakpoint_id(desc, breakpoint_id))
    {
        return breakpoint_id;
    }
    return 0;
}

bool debugger_session::try_parse_breakpoint_id(const std::string &description, std::uint32_t &breakpoint_id)
{
    breakpoint_id = 0;
    if (trim_both(description).empty())
    {
        return false;
    }
    return try_parse_breakpoint_id_after_token(description, "breakpoint", breakpoint_id) ||
           try_parse_breakpoint_id_after_token(description, "bp", breakpoint_id);
}
} // namespace dap_dbgeng::debugger
