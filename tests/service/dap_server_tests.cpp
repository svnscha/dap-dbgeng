// In-process gtests for the DAP server dispatch core.
#include <gtest/gtest.h>

#include "service/dap_server.h"
#include "transport/dap_message_writer.h"

namespace
{
// Captures every message the server sends, in order, so the test can assert on
// the outbound sequence without a real transport / stdio.
class capturing_writer : public dap_dbgeng::transport::dap_message_writer
{
  public:
    std::vector<nlohmann::json> messages;

    void send(nlohmann::json message) override
    {
        messages.push_back(std::move(message));
    }
};

TEST(ServiceDapServer, InitializeRespondsWithCapabilitiesThenInitializedEvent)
{
    capturing_writer writer;
    dap_dbgeng::service::dap_server server(writer);

    // The first inbound message from basic-debug.json.
    const auto request = nlohmann::json::parse(R"({
        "command": "initialize",
        "arguments": {
            "clientID": "vscode",
            "adapterID": "windbg",
            "pathFormat": "path",
            "linesStartAt1": true,
            "columnsStartAt1": true
        },
        "type": "request",
        "seq": 1
    })");

    const bool should_exit = server.handle_request(request);
    EXPECT_FALSE(should_exit);

    ASSERT_EQ(writer.messages.size(), 2u);

    const nlohmann::json &response = writer.messages[0];
    EXPECT_EQ(response.at("type"), "response");
    EXPECT_EQ(response.at("command"), "initialize");
    EXPECT_TRUE(response.at("success").get<bool>());
    EXPECT_EQ(response.at("request_seq"), 1);
    ASSERT_TRUE(response.contains("body"));
    EXPECT_TRUE(response.at("body").at("supportsConfigurationDoneRequest").get<bool>());
    EXPECT_FALSE(response.at("body").at("supportsSingleThreadExecutionRequests").get<bool>());

    const nlohmann::json &event = writer.messages[1];
    EXPECT_EQ(event.at("type"), "event");
    EXPECT_EQ(event.at("event"), "initialized");
}

TEST(ServiceDapServer, UnsupportedCommandRepliesUnsupported)
{
    capturing_writer writer;
    dap_dbgeng::service::dap_server server(writer);

    const auto request = nlohmann::json::parse(R"({"type":"request","command":"completions","seq":5})");

    const bool should_exit = server.handle_request(request);
    EXPECT_FALSE(should_exit);

    ASSERT_EQ(writer.messages.size(), 1u);

    const nlohmann::json &response = writer.messages[0];
    EXPECT_EQ(response.at("type"), "response");
    EXPECT_FALSE(response.at("success").get<bool>());
    EXPECT_EQ(response.at("command"), "completions");
    EXPECT_EQ(response.at("request_seq"), 5);
    EXPECT_NE(response.at("body").at("error").at("format").get<std::string>().find("Unsupported command"),
              std::string::npos);
}

TEST(ServiceDapServer, DoubleInitializeRepliesWithError)
{
    capturing_writer writer;
    dap_dbgeng::service::dap_server server(writer);

    const auto request = nlohmann::json::parse(R"({"type":"request","command":"initialize","seq":1,"arguments":{}})");
    server.handle_request(request);
    writer.messages.clear();

    const auto second = nlohmann::json::parse(R"({"type":"request","command":"initialize","seq":2,"arguments":{}})");
    server.handle_request(second);

    ASSERT_EQ(writer.messages.size(), 1u);
    const nlohmann::json &response = writer.messages[0];
    EXPECT_EQ(response.at("command"), "initialize");
    EXPECT_FALSE(response.at("success").get<bool>());
    EXPECT_EQ(response.at("request_seq"), 2);
}
} // namespace
