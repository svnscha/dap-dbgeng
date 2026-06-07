// In-process, skippable live-session gtests for the DAP server. These drive a
// real dbgeng engine end-to-end through the dap_server. They SKIP when dbgeng or
// the native test app is missing. Full end-to-end coverage comes from replay;
// this is a representative few.
#include <gtest/gtest.h>

#include "service/dap_server.h"
#include "support/test_environment.h"
#include "transport/dap_message_writer.h"

namespace
{
namespace fs = std::filesystem;
using dap_dbgeng::service::dap_server;
using namespace dap_dbgeng::test_support;

std::string parent_directory(const std::string &path)
{
    return path.empty() ? std::string() : fs::path(path).parent_path().string();
}

// Thread-safe recording writer with a wait-for helper (the wait-for-session-event
// loop pushes stopped/exited/terminated events from a background thread).
class recording_message_writer : public dap_dbgeng::transport::dap_message_writer
{
  public:
    void send(nlohmann::json message) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.push_back(std::move(message));
        cv_.notify_all();
    }

    // Wait until a message satisfying `predicate` exists, or timeout. Returns it.
    std::optional<nlohmann::json> wait_for(const std::function<bool(const nlohmann::json &)> &predicate,
                                           int timeout_ms = 15000)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (true)
        {
            for (const auto &message : messages_)
            {
                if (predicate(message))
                {
                    return std::optional<nlohmann::json>(message);
                }
            }
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout)
            {
                return std::nullopt;
            }
        }
    }

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<nlohmann::json> messages_;
};

nlohmann::json make_request(int seq, const std::string &command, nlohmann::json arguments)
{
    return nlohmann::json{{"seq", seq}, {"type", "request"}, {"command", command}, {"arguments", std::move(arguments)}};
}

bool is_response(const nlohmann::json &m, const std::string &command, int seq)
{
    return m.value("type", std::string{}) == "response" && m.value("command", std::string{}) == command &&
           m.value("request_seq", -1) == seq && m.value("success", false);
}

bool is_stopped(const nlohmann::json &m, const std::string &reason)
{
    return m.value("type", std::string{}) == "event" && m.value("event", std::string{}) == "stopped" &&
           m.contains("body") && m["body"].value("reason", std::string{}) == reason;
}

TEST(DapServerLive, LaunchStopsAtEntryAndEnumeratesThreadsAndStack)
{
    const std::string dbgeng = resolve_dbgeng_path();
    const std::string target = resolve_launch_target_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not available (set DAP_DBGENG_WINDBG_PATH).");
    DAP_REQUIRE_OR_SKIP(target, "test_launch.exe not built (set DAP_DBGENG_NATIVE_APP or build test-targets/testapp).");
    const std::string working = parent_directory(target);

    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "initialize", {{"adapterID", "dap-dbgeng-tests"}}));
    ASSERT_TRUE(writer.wait_for([](const nlohmann::json &m) { return is_response(m, "initialize", 1); }).has_value());

    server.handle_request(make_request(2, "launch",
                                       {{"name", "live-launch-test"},
                                        {"request", "launch"},
                                        {"type", "windbg"},
                                        {"program", target},
                                        {"cwd", working},
                                        {"dbgengPath", dbgeng},
                                        {"symbolPath", nlohmann::json::array({working})},
                                        {"stopAtEntry", true}}));

    ASSERT_TRUE(writer.wait_for([](const nlohmann::json &m) { return is_response(m, "launch", 2); }).has_value());
    const auto entry = writer.wait_for([](const nlohmann::json &m) { return is_stopped(m, "entry"); });
    ASSERT_TRUE(entry.has_value());
    const int thread_id = entry->at("body").at("threadId").get<int>();
    EXPECT_GT(thread_id, 0);

    server.handle_request(make_request(3, "threads", nlohmann::json::object()));
    const auto threads = writer.wait_for([](const nlohmann::json &m) { return is_response(m, "threads", 3); });
    ASSERT_TRUE(threads.has_value());
    EXPECT_GT(threads->at("body").at("threads").size(), 0u);

    server.handle_request(make_request(4, "stackTrace", {{"threadId", thread_id}, {"startFrame", 0}, {"levels", 20}}));
    const auto stack = writer.wait_for([](const nlohmann::json &m) { return is_response(m, "stackTrace", 4); });
    ASSERT_TRUE(stack.has_value());
    EXPECT_GT(stack->at("body").at("stackFrames").size(), 0u);

    // Teardown: terminate the launched process and end the session.
    server.handle_request(make_request(99, "disconnect", {{"terminateDebuggee", true}}));
}
} // namespace
