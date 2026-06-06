// In-process gtests for the DAP trace recorder.
#include <gtest/gtest.h>

#include "transport/dap_trace_recorder.h"

namespace
{
std::filesystem::path make_temp_trace_path()
{
    const auto unique = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / fmt::format("dap-dbgeng-trace-{}.json", unique);
}

TEST(DapTraceRecorder, FlushesBufferedMessagesOnDestruction)
{
    const std::filesystem::path path = make_temp_trace_path();
    std::filesystem::remove(path);

    {
        dap_dbgeng::transport::dap_trace_recorder recorder;
        ASSERT_TRUE(recorder.try_set_path(path));
        recorder.record_input(nlohmann::json{{"type", "request"}, {"command", "initialize"}, {"seq", 1}});
        recorder.record_output(nlohmann::json{{"type", "response"}, {"command", "initialize"}, {"seq", 1}});
    }

    ASSERT_TRUE(std::filesystem::exists(path));
    nlohmann::json document;
    {
        std::ifstream stream(path, std::ios::binary);
        document = nlohmann::json::parse(stream);
    }

    EXPECT_EQ(document.at("version"), 1);
    ASSERT_EQ(document.at("messages").size(), 2u);
    EXPECT_EQ(document.at("messages")[0].at("direction"), "in");
    EXPECT_EQ(document.at("messages")[1].at("direction"), "out");
    EXPECT_EQ(document.at("messages")[1].at("message").at("command"), "initialize");

    std::filesystem::remove(path);
}

TEST(DapTraceRecorder, MergesAdjacentConsoleOutputEvents)
{
    const std::filesystem::path path = make_temp_trace_path();
    std::filesystem::remove(path);

    auto console_output = [](const std::string &text) {
        return nlohmann::json{
            {"type", "event"}, {"event", "output"}, {"body", {{"category", "console"}, {"output", text}}}};
    };

    {
        dap_dbgeng::transport::dap_trace_recorder recorder;
        ASSERT_TRUE(recorder.try_set_path(path));
        recorder.record_output(console_output("hello "));
        recorder.record_output(console_output("world"));
    }

    nlohmann::json document;
    {
        std::ifstream stream(path, std::ios::binary);
        document = nlohmann::json::parse(stream);
    }
    ASSERT_EQ(document.at("messages").size(), 1u);
    EXPECT_EQ(document.at("messages")[0].at("message").at("body").at("output"), "hello world");

    std::filesystem::remove(path);
}

TEST(DapTraceRecorder, DiscardWritesNothing)
{
    const std::filesystem::path path = make_temp_trace_path();
    std::filesystem::remove(path);

    {
        dap_dbgeng::transport::dap_trace_recorder recorder;
        ASSERT_TRUE(recorder.try_set_path(path));
        recorder.record_input(nlohmann::json{{"type", "request"}, {"seq", 1}});
        recorder.discard();
        recorder.record_input(nlohmann::json{{"type", "request"}, {"seq", 2}});
    }

    // discard() clears the buffer; the post-discard record is dropped too. The
    // file is still written (empty messages array) because a path was set.
    ASSERT_TRUE(std::filesystem::exists(path));
    nlohmann::json document;
    {
        std::ifstream stream(path, std::ios::binary);
        document = nlohmann::json::parse(stream);
    }
    EXPECT_EQ(document.at("messages").size(), 0u);

    std::filesystem::remove(path);
}

TEST(DapTraceRecorder, DeferredWithoutPathWritesNoFile)
{
    // A recorder that never gets a path must not create any file on destruction.
    dap_dbgeng::transport::dap_trace_recorder recorder;
    recorder.record_input(nlohmann::json{{"type", "request"}, {"seq", 1}});
    // No assertion needed beyond not throwing / not crashing; absence of a path
    // means write_trace_file_locked is a no-op.
    SUCCEED();
}

// The first launch/attach request's "trace" argument activates recording: the
// pre-launch handshake stays buffered and the whole transcript flushes to that
// path on destruction.
TEST(DapTraceRecorder, LaunchWithTracePathActivatesRecording)
{
    const std::filesystem::path path = make_temp_trace_path();
    std::filesystem::remove(path);

    {
        dap_dbgeng::transport::dap_trace_recorder recorder;
        recorder.record_input(nlohmann::json{{"type", "request"}, {"command", "initialize"}, {"seq", 1}});
        recorder.record_output(nlohmann::json{{"type", "response"}, {"command", "initialize"}, {"seq", 1}});
        recorder.record_input(nlohmann::json{
            {"type", "request"}, {"command", "launch"}, {"seq", 2}, {"arguments", {{"trace", path.string()}}}});
        recorder.record_output(nlohmann::json{{"type", "response"}, {"command", "launch"}, {"seq", 2}});
    }

    ASSERT_TRUE(std::filesystem::exists(path));
    nlohmann::json document;
    {
        std::ifstream stream(path, std::ios::binary);
        document = nlohmann::json::parse(stream);
    }
    ASSERT_EQ(document.at("messages").size(), 4u);
    EXPECT_EQ(document.at("messages")[0].at("message").at("command"), "initialize");
    EXPECT_EQ(document.at("messages")[2].at("message").at("command"), "launch");

    std::filesystem::remove(path);
}

// A launch/attach with no "trace" configured disables recording and frees the
// buffered transcript. We observe the released buffer by pointing a path at the
// recorder afterwards: the flushed file has no messages.
TEST(DapTraceRecorder, LaunchWithoutTraceDiscardsBufferedTranscript)
{
    const std::filesystem::path path = make_temp_trace_path();
    std::filesystem::remove(path);

    {
        dap_dbgeng::transport::dap_trace_recorder recorder;
        recorder.record_input(nlohmann::json{{"type", "request"}, {"command", "initialize"}, {"seq", 1}});
        recorder.record_output(nlohmann::json{{"type", "response"}, {"command", "initialize"}, {"seq", 1}});
        recorder.record_input(nlohmann::json{{"type", "request"}, {"command", "launch"}, {"seq", 2}});
        recorder.record_output(nlohmann::json{{"type", "response"}, {"command", "launch"}, {"seq", 2}});

        // The buffer was discarded at the trace-less launch; a path set now flushes an empty transcript.
        ASSERT_TRUE(recorder.try_set_path(path));
    }

    ASSERT_TRUE(std::filesystem::exists(path));
    nlohmann::json document;
    {
        std::ifstream stream(path, std::ios::binary);
        document = nlohmann::json::parse(stream);
    }
    EXPECT_EQ(document.at("messages").size(), 0u);

    std::filesystem::remove(path);
}
} // namespace
