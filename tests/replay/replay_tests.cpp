// Out-of-process recorded-session replay tests. Each TEST replays a recorded
// fixture against a fresh dap-dbgeng.exe over stdio. The sequence match (type /
// command / success / event + ordering) is enforced inline by the replay loop;
// these tests add the per-fixture body assertions on top, all JSON-based.
#include <gtest/gtest.h>

#include "replay/replay_harness.h"

#include "support/test_environment.h"

namespace
{
using namespace dap_dbgeng::replay;

#if defined(_WIN32)

// Run a fixture through the harness, mapping unmet preconditions to GTEST_SKIP
// and surfacing the reason.
#define REPLAY_OR_SKIP(out_result, file_name)                                                                          \
    replay_result out_result;                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        try                                                                                                            \
        {                                                                                                              \
            out_result = ::dap_dbgeng::replay::replay((file_name));                                                    \
        }                                                                                                              \
        catch (const ::dap_dbgeng::replay::replay_skip &skip)                                                          \
        {                                                                                                              \
            GTEST_SKIP() << skip.what();                                                                               \
        }                                                                                                              \
    } while (false)

// --- JSON query helpers over a replay_result -------------------------------

bool is_response(const nlohmann::json &m, const char *command)
{
    return m.is_object() && m.value("type", std::string{}) == "response" &&
           m.value("command", std::string{}) == command;
}

bool is_event(const nlohmann::json &m, const char *event)
{
    return m.is_object() && m.value("type", std::string{}) == "event" && m.value("event", std::string{}) == event;
}

std::size_t count_responses(const std::vector<nlohmann::json> &messages, const char *command)
{
    return static_cast<std::size_t>(std::count_if(messages.begin(), messages.end(),
                                                  [&](const nlohmann::json &m) { return is_response(m, command); }));
}

std::size_t count_events(const std::vector<nlohmann::json> &messages, const char *event)
{
    return static_cast<std::size_t>(
        std::count_if(messages.begin(), messages.end(), [&](const nlohmann::json &m) { return is_event(m, event); }));
}

bool has_error_response(const std::vector<nlohmann::json> &messages)
{
    return std::any_of(messages.begin(), messages.end(), [](const nlohmann::json &m) {
        // An error response is a failed response (success == false).
        return m.is_object() && m.value("type", std::string{}) == "response" && !m.value("success", true);
    });
}

const nlohmann::json *single_process_event(const std::vector<nlohmann::json> &messages)
{
    const nlohmann::json *found = nullptr;
    for (const auto &m : messages)
    {
        if (is_event(m, "process"))
        {
            EXPECT_EQ(found, nullptr) << "Expected exactly one process event.";
            found = &m;
        }
    }
    return found;
}

void assert_positive_launch_replay(const replay_result &replay)
{
    EXPECT_GT(replay.non_output.size(), 0u);
    EXPECT_EQ(1u, count_responses(replay.non_output, "initialize"));
    EXPECT_EQ(1u, count_responses(replay.non_output, "launch"));
    EXPECT_FALSE(has_error_response(replay.all));

    const nlohmann::json *process = single_process_event(replay.non_output);
    ASSERT_NE(process, nullptr);
    EXPECT_EQ("launch", process->at("body").value("startMethod", std::string{}));
}

void assert_positive_attach_replay(const replay_result &replay)
{
    EXPECT_GT(replay.non_output.size(), 0u);
    EXPECT_EQ(1u, count_responses(replay.non_output, "initialize"));
    EXPECT_EQ(1u, count_responses(replay.non_output, "attach"));

    const nlohmann::json *process = single_process_event(replay.non_output);
    ASSERT_NE(process, nullptr);
    EXPECT_EQ("attach", process->at("body").value("startMethod", std::string{}));
}

// ---------------------------------------------------------------------------
// Tests (one per fixture, suite `Replay`). gtest runs serially by default.
// ---------------------------------------------------------------------------

TEST(Replay, BasicDebugSession)
{
    REPLAY_OR_SKIP(replay, "basic-debug.json");

    EXPECT_GT(replay.non_output.size(), 0u);
    EXPECT_GT(count_events(replay.all, "output"), 0u);
    EXPECT_EQ(1u, count_responses(replay.non_output, "initialize"));
    EXPECT_EQ(1u, count_responses(replay.non_output, "launch"));

    const nlohmann::json *process = single_process_event(replay.non_output);
    ASSERT_NE(process, nullptr);
    EXPECT_EQ("launch", process->at("body").value("startMethod", std::string{}));

    // stopAtEntry is false in this fixture, so the adapter must not surface an
    // entry stop; the session stops on its breakpoint instead.
    const bool entry_stop =
        std::any_of(replay.non_output.begin(), replay.non_output.end(), [](const nlohmann::json &m) {
            return is_event(m, "stopped") && m.contains("body") &&
                   m.at("body").value("reason", std::string{}) == "entry";
        });
    EXPECT_FALSE(entry_stop) << "stopAtEntry is false; expected no 'entry' stop.";

    const bool breakpoint_stop =
        std::any_of(replay.non_output.begin(), replay.non_output.end(), [](const nlohmann::json &m) {
            return is_event(m, "stopped") && m.contains("body") &&
                   m.at("body").value("reason", std::string{}) == "breakpoint";
        });
    EXPECT_TRUE(breakpoint_stop) << "Expected a stopped event with reason 'breakpoint'.";

    const bool stack_frames =
        std::any_of(replay.non_output.begin(), replay.non_output.end(), [](const nlohmann::json &m) {
            return is_response(m, "stackTrace") && m.contains("body") && m.at("body").contains("stackFrames") &&
                   m.at("body").at("stackFrames").is_array() && !m.at("body").at("stackFrames").empty();
        });
    EXPECT_TRUE(stack_frames) << "Expected a stackTrace response with non-empty stackFrames.";

    std::size_t variables_responses = 0;
    bool variables_non_empty = false;
    for (const auto &m : replay.non_output)
    {
        if (is_response(m, "variables"))
        {
            ++variables_responses;
            variables_non_empty = m.contains("body") && m.at("body").contains("variables") &&
                                  m.at("body").at("variables").is_array() && !m.at("body").at("variables").empty();
        }
    }
    EXPECT_EQ(1u, variables_responses) << "Expected exactly one variables response.";
    EXPECT_TRUE(variables_non_empty) << "Expected the variables response body to be non-empty.";

    EXPECT_EQ(1u, count_events(replay.non_output, "exited"));
    EXPECT_GE(count_events(replay.non_output, "terminated"), 1u);
}

TEST(Replay, LaunchStopAtEntryStop)
{
    REPLAY_OR_SKIP(replay, "launch-stopAtEntry-stop.json");
    assert_positive_launch_replay(replay);
}

TEST(Replay, LaunchStopAtEntryContinueExit)
{
    REPLAY_OR_SKIP(replay, "launch-stopAtEntry-continue-exit.json");
    assert_positive_launch_replay(replay);
}

TEST(Replay, LaunchStopAtEntryBreakpointContinueExit)
{
    REPLAY_OR_SKIP(replay, "launch-stopAtEntry-breakpoint-continue-exit.json");
    assert_positive_launch_replay(replay);
}

TEST(Replay, LaunchBreakpointStepOverExit)
{
    REPLAY_OR_SKIP(replay, "launch-breakpoint-stepover-exit.json");
    assert_positive_launch_replay(replay);
}

TEST(Replay, LaunchStepInOverOut)
{
    REPLAY_OR_SKIP(replay, "launch-step-in-over-out.json");
    assert_positive_launch_replay(replay);
}

TEST(Replay, LaunchConditionalBreakpoint)
{
    REPLAY_OR_SKIP(replay, "launch-conditional-breakpoint.json");
    assert_positive_launch_replay(replay);
}

TEST(Replay, LaunchBreakpointOpenDisassembly)
{
    REPLAY_OR_SKIP(replay, "launch-breakpoint-open-disassembly.json");
    assert_positive_launch_replay(replay);
}

TEST(Replay, LaunchDisassemblyStep)
{
    REPLAY_OR_SKIP(replay, "launch-disassembly-step.json");
    assert_positive_launch_replay(replay);
}

TEST(Replay, LaunchStepRestartRun)
{
    REPLAY_OR_SKIP(replay, "launch-step-restart-run.json");
    assert_positive_launch_replay(replay);
}

// Live cross-process attach (and the dbgsrv process server) cannot run on hosted
// CI runners, which now ship dbgeng.dll (so these no longer skip on a missing
// engine) but block one process from debugging another: the attach never
// completes and the replay times out. The workflows set
// DAP_DBGENG_SKIP_LIVE_ATTACH so these skip there; locally and on a self-hosted
// runner with live debugging they run normally.
#define SKIP_IF_LIVE_ATTACH_DISABLED()                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::dap_dbgeng::test_support::env_var("DAP_DBGENG_SKIP_LIVE_ATTACH"))                                        \
        {                                                                                                              \
            GTEST_SKIP() << "DAP_DBGENG_SKIP_LIVE_ATTACH set: this host cannot perform live cross-process attach.";    \
        }                                                                                                              \
    } while (false)

TEST(Replay, AttachToRunningProcess)
{
    SKIP_IF_LIVE_ATTACH_DISABLED();
    REPLAY_OR_SKIP(replay, "attach-process.json");
    assert_positive_attach_replay(replay);
}

TEST(Replay, AttachRemoteViaProcessServer)
{
    SKIP_IF_LIVE_ATTACH_DISABLED();
    REPLAY_OR_SKIP(replay, "attach-remote-process.json");
    assert_positive_attach_replay(replay);
}

TEST(Replay, LaunchSetVariable)
{
    REPLAY_OR_SKIP(replay, "launch-setVariable.json");
    assert_positive_launch_replay(replay);
}

TEST(Replay, ThreadsRequestDuringRecordedExit)
{
    REPLAY_OR_SKIP(replay, "threads-after-exit.json");

    // Find the threads response for request_seq 4 (the threads request issued
    // during the recorded exit).
    const nlohmann::json *threads_response = nullptr;
    for (const auto &m : replay.non_output)
    {
        if (is_response(m, "threads") && m.value("request_seq", -1) == 4)
        {
            ASSERT_EQ(threads_response, nullptr) << "Expected a single threads response for request_seq 4.";
            threads_response = &m;
        }
    }

    // Single exited / terminated events.
    // The process exit emits a thread(exited) for every live thread, and how many
    // threads the loader/CRT spin up is environment-dependent, so there may be more
    // than one. We validate the debuggee thread that has a full started -> exited
    // lifecycle (its started event was captured), not the count.
    const nlohmann::json *exited_event = nullptr;
    const nlohmann::json *terminated_event = nullptr;
    std::vector<const nlohmann::json *> exiting_thread_events;
    for (const auto &m : replay.non_output)
    {
        if (is_event(m, "exited"))
        {
            ASSERT_EQ(exited_event, nullptr) << "Expected a single exited event.";
            exited_event = &m;
        }
        if (is_event(m, "terminated"))
        {
            ASSERT_EQ(terminated_event, nullptr) << "Expected a single terminated event.";
            terminated_event = &m;
        }
        if (is_event(m, "thread") && m.contains("body") && m.at("body").value("reason", std::string{}) == "exited")
        {
            exiting_thread_events.push_back(&m);
        }
    }

    auto has_started_event = [&](long long thread_id) {
        return std::any_of(replay.non_output.begin(), replay.non_output.end(), [&](const nlohmann::json &m) {
            return is_event(m, "thread") && m.contains("body") &&
                   m.at("body").value("reason", std::string{}) == "started" &&
                   m.at("body").value("threadId", -2) == thread_id;
        });
    };

    // Pick the exiting thread event whose thread also has a started event.
    const nlohmann::json *exiting_thread_event = nullptr;
    for (const nlohmann::json *event : exiting_thread_events)
    {
        if (has_started_event(event->at("body").value("threadId", -1)))
        {
            exiting_thread_event = event;
            break;
        }
    }

    ASSERT_NE(threads_response, nullptr);
    ASSERT_NE(exited_event, nullptr);
    ASSERT_NE(terminated_event, nullptr);
    ASSERT_FALSE(exiting_thread_events.empty()) << "Expected at least one exiting thread event.";
    ASSERT_NE(exiting_thread_event, nullptr) << "Expected an exiting thread event whose thread also started.";

    EXPECT_TRUE(threads_response->value("success", false));
    EXPECT_EQ("threads", threads_response->value("command", std::string{}));
    ASSERT_TRUE(threads_response->contains("body") && threads_response->at("body").contains("threads"));
    EXPECT_TRUE(threads_response->at("body").at("threads").is_array());
    EXPECT_EQ(0u, threads_response->at("body").at("threads").size());

    // Ordering: thread(exited) < exited < terminated < threads-response(seq 4).
    auto index_of = [&](const nlohmann::json *target) -> std::ptrdiff_t {
        for (std::size_t i = 0; i < replay.non_output.size(); ++i)
        {
            if (&replay.non_output[i] == target)
            {
                return static_cast<std::ptrdiff_t>(i);
            }
        }
        return -1;
    };
    const std::ptrdiff_t thread_exited_index = index_of(exiting_thread_event);
    const std::ptrdiff_t exited_index = index_of(exited_event);
    const std::ptrdiff_t terminated_index = index_of(terminated_event);
    const std::ptrdiff_t threads_response_index = index_of(threads_response);

    EXPECT_LT(thread_exited_index, exited_index);
    EXPECT_LT(exited_index, terminated_index);
    EXPECT_LT(terminated_index, threads_response_index);

    // No error response for request_seq 4.
    const bool error_for_seq4 = std::any_of(replay.all.begin(), replay.all.end(), [](const nlohmann::json &m) {
        return m.is_object() && m.value("type", std::string{}) == "response" && !m.value("success", true) &&
               m.value("command", std::string{}) == "threads" && m.value("request_seq", -1) == 4;
    });
    EXPECT_FALSE(error_for_seq4);
}

#endif // _WIN32
} // namespace
