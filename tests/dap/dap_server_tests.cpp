#include <gtest/gtest.h>

#include "dap/dap_server.h"

namespace
{
TEST(FrameMessage, PrependsContentLengthHeader)
{
    const nlohmann::json message = {{"type", "event"}, {"event", "initialized"}};

    const std::string framed = dap_dbgeng::frame_message(message);
    const std::string body = message.dump();

    EXPECT_EQ(framed, "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
}

TEST(FrameMessage, BodyIsValidJsonRoundTrip)
{
    const nlohmann::json message = {{"seq", 1}, {"type", "request"}, {"command", "initialize"}};

    const std::string framed = dap_dbgeng::frame_message(message);
    const auto separator = framed.find("\r\n\r\n");
    ASSERT_NE(separator, std::string::npos);

    const std::string body = framed.substr(separator + 4);
    EXPECT_EQ(nlohmann::json::parse(body), message);
}
} // namespace
