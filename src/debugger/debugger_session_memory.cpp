#include "debugger/debugger_session.h"

#include "debugger/debugger_session_internal.h"

namespace dap_dbgeng::debugger
{
std::optional<disassembled_instruction_info> debugger_session::disassemble_one(std::uint64_t address,
                                                                               bool resolve_symbols,
                                                                               std::uint64_t &end_offset)
{
    end_offset = address;

    std::vector<char> buffer(kDisassemblyLineBytes);
    ULONG disassembly_size = 0;
    ULONG64 next = 0;
    const HRESULT hr =
        control_->Disassemble(address, 0, buffer.data(), static_cast<ULONG>(buffer.size()), &disassembly_size, &next);
    if (FAILED(hr) || next <= address)
    {
        return std::nullopt;
    }
    end_offset = next;

    disassembled_instruction_info info;
    info.address = address;
    info.instruction = extract_instruction_text(std::string(buffer.data()));

    // Raw bytes straight from memory rather than from the disassembly text.
    const std::uint64_t length = next - address;
    if (data_spaces_ != nullptr && length > 0 && length <= 16)
    {
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
        ULONG read = 0;
        if (SUCCEEDED(data_spaces_->ReadVirtual(address, bytes.data(), static_cast<ULONG>(bytes.size()), &read)) &&
            read > 0)
        {
            bytes.resize(read);
            info.instruction_bytes = format_hex_bytes(bytes);
        }
    }

    if (resolve_symbols)
    {
        info.symbol = try_get_frame_name(address);
    }
    return info;
}

std::vector<disassembled_instruction_info> debugger_session::disassemble_range(std::uint64_t start_address, int count,
                                                                               bool resolve_symbols)
{
    std::vector<disassembled_instruction_info> result;
    if (count <= 0)
    {
        return result;
    }
    result.reserve(count);

    std::uint64_t address = start_address;
    for (int i = 0; i < count; ++i)
    {
        std::uint64_t next = 0;
        std::optional<disassembled_instruction_info> info = disassemble_one(address, resolve_symbols, next);
        if (!info)
        {
            break; // undecodable byte; the caller pads with invalid placeholders
        }
        result.push_back(std::move(*info));
        address = next;
    }
    return result;
}

std::vector<disassembled_instruction_info> debugger_session::disassemble_backward(std::uint64_t end_address, int count,
                                                                                  bool resolve_symbols)
{
    std::vector<disassembled_instruction_info> result;
    if (count <= 0 || end_address == 0)
    {
        return result;
    }

    // x86/x64 instructions are at most 15 bytes. Scan candidate start addresses in a
    // window before end_address; the earliest start whose forward decode lands an
    // instruction boundary exactly on end_address yields the most preceding
    // instructions (a misaligned start eventually overruns the boundary).
    constexpr std::uint64_t kMaxInstructionBytes = 15;
    const std::uint64_t window = std::min<std::uint64_t>(
        end_address, kMaxInstructionBytes * (static_cast<std::uint64_t>(count) + 1) + kMaxInstructionBytes);
    const std::uint64_t scan_start = end_address - window;

    for (std::uint64_t probe = scan_start; probe < end_address; ++probe)
    {
        std::vector<disassembled_instruction_info> decoded;
        std::uint64_t address = probe;
        bool aligned = false;
        while (address < end_address)
        {
            std::uint64_t next = 0;
            std::optional<disassembled_instruction_info> info = disassemble_one(address, resolve_symbols, next);
            if (!info)
            {
                break;
            }
            decoded.push_back(std::move(*info));
            if (next == end_address)
            {
                aligned = true;
                break;
            }
            if (next <= address || next > end_address)
            {
                break; // overran the boundary: this start is misaligned
            }
            address = next;
        }

        if (aligned)
        {
            if (static_cast<int>(decoded.size()) > count)
            {
                decoded.erase(decoded.begin(), decoded.begin() + (static_cast<int>(decoded.size()) - count));
            }
            return decoded;
        }
    }
    return result;
}

std::vector<disassembled_instruction_info> debugger_session::disassemble(std::uint64_t memory_address,
                                                                         int instruction_offset, int instruction_count,
                                                                         bool resolve_symbols)
{
    throw_if_disposed();
    if (instruction_count < 0)
    {
        throw std::out_of_range("instructionCount");
    }
    if (instruction_count == 0)
    {
        return {};
    }

    std::vector<disassembled_instruction_info> instructions;
    if (instruction_offset < 0)
    {
        const int previous_instruction_count = std::min(-instruction_offset, instruction_count);
        std::vector<disassembled_instruction_info> previous_instructions;

        std::uint64_t symbol_start = 0;
        if (try_get_symbol_start(memory_address, symbol_start) && symbol_start <= memory_address)
        {
            const std::uint64_t requested = std::min<std::uint64_t>(
                static_cast<std::uint64_t>(std::numeric_limits<int>::max()),
                (memory_address - symbol_start) + static_cast<std::uint64_t>(instruction_count));
            const int requested_instruction_count = static_cast<int>(requested);
            std::vector<disassembled_instruction_info> symbol_instructions =
                disassemble_range(symbol_start, requested_instruction_count, resolve_symbols);

            // Where(addr < memory) then TakeLast(previousInstructionCount).
            std::vector<disassembled_instruction_info> before;
            for (auto &instruction : symbol_instructions)
            {
                if (instruction.address < memory_address)
                {
                    before.push_back(instruction);
                }
            }
            if (static_cast<int>(before.size()) > previous_instruction_count)
            {
                before.erase(before.begin(),
                             before.begin() + (static_cast<int>(before.size()) - previous_instruction_count));
            }
            previous_instructions = std::move(before);
        }
        else
        {
            previous_instructions = disassemble_backward(memory_address, previous_instruction_count, resolve_symbols);
        }

        const int missing_leading = previous_instruction_count - static_cast<int>(previous_instructions.size());
        if (missing_leading > 0)
        {
            const std::uint64_t first_available =
                !previous_instructions.empty() ? previous_instructions.front().address : memory_address;
            auto leading = create_invalid_leading_instructions(first_available, missing_leading);
            instructions.insert(instructions.end(), leading.begin(), leading.end());
        }

        instructions.insert(instructions.end(), previous_instructions.begin(), previous_instructions.end());

        const int remaining = instruction_count - static_cast<int>(instructions.size());
        if (remaining > 0)
        {
            auto forward = disassemble_range(memory_address, remaining, resolve_symbols);
            instructions.insert(instructions.end(), forward.begin(), forward.end());
        }
    }
    else
    {
        const int requested_instruction_count = instruction_offset + instruction_count;
        auto forward = disassemble_range(memory_address, requested_instruction_count, resolve_symbols);
        instructions.insert(instructions.end(), forward.begin(), forward.end());
        if (instruction_offset > 0 && static_cast<int>(instructions.size()) > instruction_offset)
        {
            instructions.erase(instructions.begin(), instructions.begin() + instruction_offset);
        }
        else if (instruction_offset > 0)
        {
            instructions.clear();
        }
    }

    // GroupBy(Address).First(), then Take(instructionCount), preserving order.
    {
        std::vector<disassembled_instruction_info> deduped;
        std::set<std::uint64_t> seen;
        for (auto &instruction : instructions)
        {
            if (seen.insert(instruction.address).second)
            {
                deduped.push_back(instruction);
                if (static_cast<int>(deduped.size()) == instruction_count)
                {
                    break;
                }
            }
        }
        instructions = std::move(deduped);
    }

    if (static_cast<int>(instructions.size()) == instruction_count)
    {
        return instructions;
    }

    std::uint64_t next_address = !instructions.empty() ? instructions.back().address + 1 : memory_address;
    while (static_cast<int>(instructions.size()) < instruction_count)
    {
        disassembled_instruction_info info;
        info.address = next_address;
        info.instruction = "<invalid instruction>";
        info.is_invalid = true;
        instructions.push_back(std::move(info));
        ++next_address;
    }

    return instructions;
}

std::vector<unsigned char> debugger_session::read_memory(std::uint64_t address, std::uint32_t count)
{
    throw_if_disposed();
    if (count == 0)
    {
        return {};
    }

    std::vector<unsigned char> bytes(count);
    ULONG read = 0;
    const HRESULT hr = data_spaces_->ReadVirtual(address, bytes.data(), static_cast<ULONG>(bytes.size()), &read);
    if (FAILED(hr))
    {
        // A fully unreadable range is an empty (not failed) read; the caller
        // reports it as zero bytes per DAP readMemory semantics.
        return {};
    }
    bytes.resize(read);
    return bytes;
}

std::uint32_t debugger_session::write_memory(std::uint64_t address, const std::vector<unsigned char> &bytes)
{
    throw_if_disposed();
    if (bytes.empty())
    {
        return 0;
    }

    ULONG written = 0;
    check_hr(data_spaces_->WriteVirtual(address, const_cast<unsigned char *>(bytes.data()),
                                        static_cast<ULONG>(bytes.size()), &written),
             fmt::format("Could not write {} bytes at 0x{:X}", bytes.size(), address));
    return written;
}
} // namespace dap_dbgeng::debugger
