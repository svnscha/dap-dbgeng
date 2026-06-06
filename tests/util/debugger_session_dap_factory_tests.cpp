// In-process gtests for the DAP factory + WinDbg command classifier.
#include <gtest/gtest.h>

#include "util/debugger_session_dap_factory.h"
#include "util/windbg_command_classifier.h"

namespace
{
namespace factory = dap_dbgeng::util::debugger_session_dap_factory;
namespace classifier = dap_dbgeng::util::windbg_command_classifier;

TEST(DebuggerSessionDapFactory, CreateFrameIdReturnsPositiveNonZeroId)
{
    EXPECT_GT(factory::create_frame_id(123, 456), 0);
}

TEST(DebuggerSessionDapFactory, CreateFrameIdIsStableForSameInputs)
{
    EXPECT_EQ(factory::create_frame_id(123, 456), factory::create_frame_id(123, 456));
    EXPECT_NE(factory::create_frame_id(123, 456), factory::create_frame_id(123, 457));
}

TEST(DebuggerSessionDapFactory, CreateThreadsMarksCurrentThread)
{
    const std::vector<dap_dbgeng::protocol::Thread> threads = factory::create_threads({123u, 456u}, 456);
    ASSERT_EQ(threads.size(), 2u);
    EXPECT_EQ(threads[0].name, "Thread 123");
    EXPECT_EQ(threads[1].name, "Thread 456 (current)");
}

TEST(DebuggerSessionDapFactory, CreateThreadsAboveInt32Throws)
{
    EXPECT_THROW(
        factory::create_threads({static_cast<std::uint32_t>(std::numeric_limits<int>::max()) + 1u}, std::nullopt),
        std::overflow_error);
}

TEST(WinDbgCommandClassifier, DetectsExecutionControlCommands)
{
    EXPECT_TRUE(classifier::is_execution_control_command("g"));
    EXPECT_TRUE(classifier::is_execution_control_command("G")); // case-insensitive
    EXPECT_TRUE(classifier::is_execution_control_command("p 5"));
    EXPECT_TRUE(classifier::is_execution_control_command(".echo ready; g"));
    EXPECT_TRUE(classifier::is_execution_control_command(".breakin"));
}

TEST(WinDbgCommandClassifier, IgnoresNonExecutionControlCommands)
{
    EXPECT_FALSE(classifier::is_execution_control_command("dv"));
    EXPECT_FALSE(classifier::is_execution_control_command("?? sum"));
    EXPECT_FALSE(classifier::is_execution_control_command(".echo hello"));
    EXPECT_FALSE(classifier::is_execution_control_command("garbage")); // 'g' is only a prefix, not the token
}
} // namespace
