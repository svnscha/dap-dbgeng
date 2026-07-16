#pragma once

namespace dap_dbgeng::util
{
// Standard base64 (RFC 4648, with padding) for the DAP readMemory/writeMemory
// payloads. Header-only; no project dependencies beyond the PCH.

inline std::string base64_encode(const std::vector<unsigned char> &bytes)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= bytes.size())
    {
        const unsigned int chunk = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back(kAlphabet[chunk & 0x3F]);
        i += 3;
    }
    const std::size_t remaining = bytes.size() - i;
    if (remaining == 1)
    {
        const unsigned int chunk = bytes[i] << 16;
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (remaining == 2)
    {
        const unsigned int chunk = (bytes[i] << 16) | (bytes[i + 1] << 8);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

// Decodes standard base64 (padding optional). Returns false on any character
// outside the alphabet, padding anywhere but the end of the final quantum, or
// a dangling character that encodes no byte (length % 4 == 1).
inline bool try_base64_decode(const std::string &text, std::vector<unsigned char> &bytes)
{
    bytes.clear();
    const auto value_of = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
        {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z')
        {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9')
        {
            return c - '0' + 52;
        }
        if (c == '+')
        {
            return 62;
        }
        if (c == '/')
        {
            return 63;
        }
        return -1;
    };

    unsigned int accumulator = 0;
    int bits = 0;
    int group = 0; // data characters in the current 4-character quantum
    int pad = 0;   // '=' characters closing the final quantum
    for (char c : text)
    {
        if (c == '\r' || c == '\n')
        {
            continue;
        }
        if (c == '=')
        {
            // '=' may only pad a final quantum of 2 or 3 data characters to 4.
            if (group < 2 || group + pad >= 4)
            {
                return false;
            }
            ++pad;
            continue;
        }
        if (pad != 0)
        {
            return false; // data after padding
        }
        const int value = value_of(c);
        if (value < 0)
        {
            return false;
        }
        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (++group == 4)
        {
            group = 0;
        }
        if (bits >= 8)
        {
            bits -= 8;
            bytes.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xFF));
        }
    }
    if (pad != 0)
    {
        return group + pad == 4;
    }
    return group != 1; // a single dangling character encodes no byte
}
} // namespace dap_dbgeng::util
