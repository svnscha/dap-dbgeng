// Compiles the generated protocol headers and validates serialization + dispatch.
#include <gtest/gtest.h>

namespace
{
using namespace dap_dbgeng::protocol;

TEST(Protocol, StoppedEventSerializesDiscriminatorsAndBody)
{
    StoppedEvent event;
    event.seq = 47;
    event.body.reason = StoppedEventBodyReason::Breakpoint;
    event.body.description = "Paused on breakpoint 0.";
    event.body.thread_id = 43060;
    event.body.hit_breakpoint_ids = std::vector<int>{0};

    const nlohmann::json j = event;

    EXPECT_EQ(j.at("type"), "event");
    EXPECT_EQ(j.at("event"), "stopped");
    EXPECT_EQ(j.at("seq"), 47);
    EXPECT_EQ(j.at("body").at("reason"), "breakpoint");
    EXPECT_EQ(j.at("body").at("description"), "Paused on breakpoint 0.");
    EXPECT_EQ(j.at("body").at("threadId"), 43060);
    EXPECT_EQ(j.at("body").at("hitBreakpointIds"), (nlohmann::json{0}));
}

TEST(Protocol, OptionalFieldsAreOmittedWhenAbsent)
{
    StoppedEvent event;
    event.body.reason = StoppedEventBodyReason::Step;

    const nlohmann::json j = event;

    EXPECT_FALSE(j.at("body").contains("description"));
    EXPECT_FALSE(j.at("body").contains("threadId"));
    EXPECT_FALSE(j.at("body").contains("hitBreakpointIds"));
}

TEST(Protocol, MultiWordEnumRoundTrips)
{
    const nlohmann::json j = StoppedEventBodyReason::FunctionBreakpoint;
    EXPECT_EQ(j, "function breakpoint");
    EXPECT_EQ(j.get<StoppedEventBodyReason>(), StoppedEventBodyReason::FunctionBreakpoint);
}

TEST(Protocol, UnknownEnumValueThrows)
{
    const nlohmann::json j = "no-such-reason";
    EXPECT_THROW((void)j.get<StoppedEventBodyReason>(), nlohmann::json::exception);
}

TEST(Protocol, LaunchRequestParsesTypedArguments)
{
    const auto request = nlohmann::json::parse(R"({
        "seq": 2,
        "type": "request",
        "command": "launch",
        "arguments": { "noDebug": true }
    })")
                             .get<LaunchRequest>();

    EXPECT_EQ(request.seq, 2);
    EXPECT_EQ(request.command, "launch");
    ASSERT_TRUE(request.arguments.no_debug.has_value());
    EXPECT_TRUE(*request.arguments.no_debug);
}

TEST(Protocol, InitializeResponseRoundTrips)
{
    InitializeResponse response;
    response.seq = 1;
    response.request_seq = 1;
    response.success = true;
    response.command = "initialize";
    Capabilities caps;
    caps.supports_configuration_done_request = true;
    caps.supports_terminate_request = true;
    response.body = caps;

    const nlohmann::json j = response;
    EXPECT_EQ(j.at("type"), "response");
    EXPECT_EQ(j.at("command"), "initialize");
    EXPECT_TRUE(j.at("success").get<bool>());
    EXPECT_TRUE(j.at("body").at("supportsConfigurationDoneRequest").get<bool>());
}

// A dap_service subclass that captures the typed launch request via string dispatch.
class capturing_service : public dap_service
{
  public:
    std::optional<LaunchRequest> launched;
    std::optional<std::string> unhandled_command;

  protected:
    void handle_launch_request(const LaunchRequest &request) override
    {
        launched = request;
    }
    void handle_request(const Request &request) override
    {
        unhandled_command = request.command;
    }
};

TEST(DapService, DispatchesKnownRequestToTypedHandler)
{
    capturing_service service;
    service.handle(nlohmann::json::parse(R"({"seq":2,"type":"request","command":"launch","arguments":{}})"));

    ASSERT_TRUE(service.launched.has_value());
    EXPECT_EQ(service.launched->seq, 2);
    EXPECT_FALSE(service.unhandled_command.has_value());
}

TEST(DapService, UnimplementedKnownRequestThrowsUnhandled)
{
    capturing_service service;
    try
    {
        service.handle(nlohmann::json::parse(R"({"seq":9,"type":"request","command":"goto","arguments":{}})"));
        FAIL() << "expected unhandled_dap_request";
    }
    catch (const unhandled_dap_request &exception)
    {
        EXPECT_EQ(exception.command, "goto");
        EXPECT_EQ(exception.request_seq, 9);
    }
}

TEST(DapService, UnknownRequestFallsThroughToHandleRequest)
{
    capturing_service service;
    service.handle(nlohmann::json::parse(R"({"seq":3,"type":"request","command":"madeUpCommand"})"));

    ASSERT_TRUE(service.unhandled_command.has_value());
    EXPECT_EQ(*service.unhandled_command, "madeUpCommand");
}
} // namespace
