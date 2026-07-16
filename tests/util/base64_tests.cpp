// Unit tests for the base64 helpers backing readMemory/writeMemory payloads.
#include <gtest/gtest.h>

#include "util/base64.h"

namespace
{
using dap_dbgeng::util::base64_encode;
using dap_dbgeng::util::try_base64_decode;

std::vector<unsigned char> bytes_of(std::initializer_list<int> values)
{
    std::vector<unsigned char> bytes;
    for (int v : values)
    {
        bytes.push_back(static_cast<unsigned char>(v));
    }
    return bytes;
}
} // namespace

TEST(Base64, EncodesAllPaddingLengths)
{
    EXPECT_EQ(base64_encode({}), "");
    EXPECT_EQ(base64_encode(bytes_of({'f'})), "Zg==");
    EXPECT_EQ(base64_encode(bytes_of({'f', 'o'})), "Zm8=");
    EXPECT_EQ(base64_encode(bytes_of({'f', 'o', 'o'})), "Zm9v");
    EXPECT_EQ(base64_encode(bytes_of({0x00, 0xFF, 0x10, 0x42})), "AP8QQg==");
}

TEST(Base64, DecodeRoundTripsAndAcceptsMissingPadding)
{
    std::vector<unsigned char> decoded;
    ASSERT_TRUE(try_base64_decode("Zm9v", decoded));
    EXPECT_EQ(decoded, bytes_of({'f', 'o', 'o'}));
    ASSERT_TRUE(try_base64_decode("Zg", decoded));
    EXPECT_EQ(decoded, bytes_of({'f'}));
    ASSERT_TRUE(try_base64_decode("", decoded));
    EXPECT_TRUE(decoded.empty());
}

TEST(Base64, DecodeRejectsInvalidCharacters)
{
    std::vector<unsigned char> decoded;
    EXPECT_FALSE(try_base64_decode("Zm9v!", decoded));
    EXPECT_FALSE(try_base64_decode("a b", decoded));
}

TEST(Base64, DecodeRejectsDanglingCharacters)
{
    // A single character (6 bits) encodes no byte; a truncated payload must
    // fail instead of silently writing fewer bytes.
    std::vector<unsigned char> decoded;
    EXPECT_FALSE(try_base64_decode("A", decoded));
    EXPECT_FALSE(try_base64_decode("KgAAA", decoded));
    EXPECT_TRUE(try_base64_decode("KgAAAA==", decoded));
    EXPECT_EQ(decoded, bytes_of({42, 0, 0, 0}));
}

TEST(Base64, DecodeRejectsMisplacedPadding)
{
    std::vector<unsigned char> decoded;
    EXPECT_FALSE(try_base64_decode("Zg==Zg==", decoded)); // data after padding
    EXPECT_FALSE(try_base64_decode("Zg=", decoded));      // quantum not padded to 4
    EXPECT_FALSE(try_base64_decode("Z===", decoded));     // more padding than data allows
    EXPECT_FALSE(try_base64_decode("Zm9v=", decoded));    // padding after a full quantum
    EXPECT_FALSE(try_base64_decode("=", decoded));
}

TEST(Base64, DecodeSkipsLineBreaks)
{
    std::vector<unsigned char> decoded;
    ASSERT_TRUE(try_base64_decode("Zm9v\r\nZg==", decoded));
    EXPECT_EQ(decoded, bytes_of({'f', 'o', 'o', 'f'}));
}
