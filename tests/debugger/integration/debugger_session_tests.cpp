// Integration tests for debugger_session against the live debug engine. The
// static-helper tests always run; the live-engine tests are skippable
// (GTEST_SKIP) when dbgeng or the native test app is missing, or when not on
// Windows.
#include <gtest/gtest.h>

#include "debugger/debugger_session.h"
#include "support/test_environment.h"

namespace
{
using namespace dap_dbgeng::debugger;
using namespace dap_dbgeng::test_support;
namespace fs = std::filesystem;

// Best-effort teardown of a launched session: detach, then destroy.
void cleanup_launched_session(std::unique_ptr<debugger_session> &session)
{
    if (session)
    {
        try
        {
            session->detach_all_processes();
        }
        catch (const std::exception &)
        {
        }
    }
    session.reset();
}
} // namespace

// ---------------------------------------------------------------------------
// Static helpers — always run (parameterized cases).
// ---------------------------------------------------------------------------
TEST(DebuggerSession, TryParseBreakpointIdRecognizesDbgEngBreakpointDescriptions)
{
    struct Case
    {
        const char *description;
        std::uint32_t expected;
    };
    const Case cases[] = {
        {"Breakpoint 12 hit", 12u},
        {"Last event: breakpoint 8 (Code Breakpoint) at 00007ff6`c0001050, pid: 2e30, tid: 3a18", 8u},
        {"Last event: bp 2 at 0x004010fa", 2u},
    };

    for (const auto &c : cases)
    {
        std::uint32_t actual = 0;
        EXPECT_TRUE(debugger_session::try_parse_breakpoint_id(c.description, actual)) << c.description;
        EXPECT_EQ(c.expected, actual) << c.description;
    }
}

TEST(DebuggerSession, TryParseBreakpointIdRejectsDescriptionsWithoutBreakpointIds)
{
    const char *descriptions[] = {"", "Breakpoint hit", "Last event: exception c0000005"};
    for (const char *description : descriptions)
    {
        std::uint32_t actual = 0;
        EXPECT_FALSE(debugger_session::try_parse_breakpoint_id(description, actual)) << description;
        EXPECT_EQ(0u, actual) << description;
    }
}

TEST(DebuggerSession, GetStackTraceFetchCountClampsUnboundedRequestsToReportedFrameCount)
{
    EXPECT_EQ(5, debugger_session::get_stack_trace_fetch_count(/*total*/ 5, std::numeric_limits<int>::max()));
    EXPECT_EQ(3, debugger_session::get_stack_trace_fetch_count(/*total*/ 5, /*max*/ 3));
}

#if defined(_WIN32)
// ---------------------------------------------------------------------------
// Live-engine integration tests — skippable.
// ---------------------------------------------------------------------------
TEST(DebuggerSessionIntegration, LaunchEnumeratesThreadsAndStackFrames)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_launch_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_launch.exe not found (build test-targets/testapp or set DAP_DBGENG_NATIVE_APP).");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);

        const auto thread_ids = session->get_thread_ids();
        ASSERT_GT(thread_ids.size(), 0u) << "Expected at least one thread after launch.";
        const std::uint32_t current = session->get_current_thread_id();
        EXPECT_NE(std::find(thread_ids.begin(), thread_ids.end(), current), thread_ids.end())
            << "Expected the current thread to be among the enumerated threads.";

        const auto frames = session->get_stack_trace();
        EXPECT_GT(frames.size(), 0u) << "Expected at least one stack frame at the initial launch break.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, LaunchWithBreakpointHitReturnsSourceBackedStackFrames)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_launch_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_launch.exe not found (build test-targets/testapp or set DAP_DBGENG_NATIVE_APP).");
    const std::string source = resolve_launch_target_source();
    DAP_REQUIRE_OR_SKIP(source, "test-targets/testapp/launch.cpp not found.");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);
        session->set_symbol_path({resolve_launch_target_directory()});
        session->set_source_path({fs::path(source).parent_path().string()});
        session->enable_source_line_support();
        session->set_source_breakpoints(source, std::vector<int>{10});
        session->continue_();
        session->wait_for_event(10000);

        const auto frames = session->get_stack_trace();
        ASSERT_GT(frames.size(), 0u) << "Expected stack frames after breaking in source.";
        ASSERT_TRUE(frames[0].source.has_value())
            << "Expected source information on the topmost stack frame at a source breakpoint.";
        // The PDB embeds the build-time source path, which can differ from this
        // checkout's path (the prebuilt test_launch.exe may have been compiled in
        // another tree). Validating the filename + line is what actually proves
        // source-line resolution works; the absolute path is a build artifact.
        EXPECT_EQ(fs::path(source).filename(), fs::path(frames[0].source->path).filename())
            << "Expected stack frame source file to be launch.cpp.";
        EXPECT_EQ(10, frames[0].source->line) << "Expected source line to match the breakpoint line.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, LaunchExecuteCommandWithOutputReturnsWinDbgOutput)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_launch_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_launch.exe not found (build test-targets/testapp or set DAP_DBGENG_NATIVE_APP).");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);

        const std::string output = session->execute_command_with_output("~", /*suppress_output_events=*/true);
        EXPECT_FALSE(output.empty()) << "Expected WinDbg command output at the initial launch break.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, LaunchGetLocalsAtSourceBreakpointReturnsNamedValues)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_launch_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_launch.exe not found (build test-targets/testapp or set DAP_DBGENG_NATIVE_APP).");
    const std::string source = resolve_launch_target_source();
    DAP_REQUIRE_OR_SKIP(source, "test-targets/testapp/launch.cpp not found.");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);
        session->set_symbol_path({resolve_launch_target_directory()});
        session->set_source_path({fs::path(source).parent_path().string()});
        session->enable_source_line_support();
        // Line 13 computes `sum`; locals (a, b, sum) are in scope there.
        session->set_source_breakpoints(source, std::vector<int>{13});
        session->continue_();
        session->wait_for_event(10000);

        const auto locals = session->get_locals(0);
        EXPECT_GT(locals.size(), 0u) << "Expected named locals at the source breakpoint.";
        for (const auto &local : locals)
        {
            EXPECT_FALSE(local.name.empty()) << "Every local should carry a name.";
        }

        const auto registers = session->get_registers(0);
        EXPECT_GT(registers.size(), 0u) << "Expected named registers at the source breakpoint.";
        for (const auto &reg : registers)
        {
            EXPECT_FALSE(reg.name.empty()) << "Every register should carry a name.";
        }
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, LaunchGetLocalsTreeExpandsNestedStruct)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_struct_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_struct_1.exe not found (build test-targets/testapp).");
    const std::string source = resolve_struct_target_source();
    DAP_REQUIRE_OR_SKIP(source, "test-targets/testapp/struct-1.cpp not found.");

    const auto find_child = [](const std::vector<variable_node> &nodes,
                               const std::string &name) -> const variable_node * {
        for (const auto &node : nodes)
        {
            if (node.name == name)
            {
                return &node;
            }
        }
        return nullptr;
    };

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);
        session->set_symbol_path({resolve_struct_target_directory()});
        session->set_source_path({fs::path(source).parent_path().string()});
        session->enable_source_line_support();
        // Line 37 is the cout that uses p, v, t; all three are initialized and live there.
        session->set_source_breakpoints(source, std::vector<int>{37});
        session->continue_();
        session->wait_for_event(10000);

        const auto locals = session->get_locals_tree(0);
        ASSERT_GT(locals.size(), 0u) << "Expected named locals at the source breakpoint.";

        const variable_node *t = find_child(locals, "t");
        ASSERT_NE(t, nullptr) << "Expected the nested struct local 't'.";
        EXPECT_FALSE(t->type.empty()) << "Struct locals should carry a type name.";
        ASSERT_FALSE(t->children.empty()) << "'t' is a struct and must expand to its fields.";

        const variable_node *origin = find_child(t->children, "origin");
        const variable_node *scale = find_child(t->children, "scale");
        ASSERT_NE(origin, nullptr) << "Expected 't.origin' (a point2).";
        ASSERT_NE(scale, nullptr) << "Expected 't.scale' (a vector3).";

        // Second level: point2 nested inside transform expands to its scalar fields.
        ASSERT_FALSE(origin->children.empty()) << "'t.origin' is a struct and must expand.";
        EXPECT_NE(find_child(origin->children, "x"), nullptr) << "Expected 't.origin.x'.";
        EXPECT_NE(find_child(origin->children, "y"), nullptr) << "Expected 't.origin.y'.";

        // Scalar leaf: 't.id' is an int and must not be expandable.
        const variable_node *id = find_child(t->children, "id");
        ASSERT_NE(id, nullptr) << "Expected 't.id'.";
        EXPECT_TRUE(id->children.empty()) << "A scalar field should have no children.";
        EXPECT_FALSE(id->is_expandable) << "A scalar field should not be expandable.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, LaunchDisassembleReturnsInstructions)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_launch_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_launch.exe not found (build test-targets/testapp or set DAP_DBGENG_NATIVE_APP).");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);

        const auto frames = session->get_stack_trace();
        ASSERT_GT(frames.size(), 0u) << "Expected a stack frame at the initial launch break.";

        const auto instructions =
            session->disassemble(frames.front().instruction_offset, /*offset*/ 0, /*count*/ 5, /*resolve*/ true);
        EXPECT_EQ(5u, instructions.size()) << "Disassemble pads to the requested count.";
        const bool any_valid =
            std::any_of(instructions.begin(), instructions.end(), [](const auto &i) { return !i.is_invalid; });
        EXPECT_TRUE(any_valid) << "Expected at least one real instruction at the current instruction pointer.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, LaunchAllowsSelectingEnumeratedThread)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_launch_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_launch.exe not found (build test-targets/testapp or set DAP_DBGENG_NATIVE_APP).");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);

        const auto thread_ids = session->get_thread_ids();
        ASSERT_GT(thread_ids.size(), 0u) << "Expected at least one thread after launch.";

        session->set_current_thread(thread_ids[0]);
        EXPECT_EQ(thread_ids[0], session->get_current_thread_id())
            << "Expected set_current_thread to update the current thread selection.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}
#endif // _WIN32
