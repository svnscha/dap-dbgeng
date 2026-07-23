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

TEST(DebuggerSessionIntegration, GetLocalValueReadsNestedStructFields)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_struct_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_struct_1.exe not found (build test-targets/testapp).");
    const std::string source = resolve_struct_target_source();
    DAP_REQUIRE_OR_SKIP(source, "test-targets/testapp/struct-1.cpp not found.");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);
        session->set_symbol_path({resolve_struct_target_directory()});
        session->set_source_path({fs::path(source).parent_path().string()});
        session->enable_source_line_support();
        session->set_source_breakpoints(source, std::vector<int>{37});
        session->continue_();
        session->wait_for_event(10000);

        // The watch-pane read path: nested member expressions resolve natively.
        const variable_node px = session->get_local_value(0, "p.x");
        EXPECT_EQ(px.value, "3") << "p is point2{3, 7}.";
        const variable_node py = session->get_local_value(0, "p.y");
        EXPECT_EQ(py.value, "7");
        EXPECT_THROW(session->get_local_value(0, "no_such_symbol"), std::exception);
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, SetLocalValueWritesNestedStructFields)
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
        session->set_source_breakpoints(source, std::vector<int>{37});
        session->continue_();
        session->wait_for_event(10000);

        // Nested int, nested double, and a scalar struct field, all by expression.
        const variable_node ox = session->set_local_value(0, "t.origin.x", "99");
        EXPECT_EQ(ox.value, "99");
        const variable_node vz = session->set_local_value(0, "v.z", "9.5");
        EXPECT_EQ(vz.value, "9.5");
        session->set_local_value(0, "t.id", "7");

        // Re-read the whole tree and confirm the writes stuck.
        const auto locals = session->get_locals_tree(0);
        const variable_node *t = find_child(locals, "t");
        ASSERT_NE(t, nullptr);
        const variable_node *origin = find_child(t->children, "origin");
        ASSERT_NE(origin, nullptr);
        const variable_node *x = find_child(origin->children, "x");
        ASSERT_NE(x, nullptr);
        EXPECT_EQ(x->value, "99") << "t.origin.x should reflect the native write.";
        const variable_node *id = find_child(t->children, "id");
        ASSERT_NE(id, nullptr);
        EXPECT_EQ(id->value, "7") << "t.id should reflect the native write.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, LaunchGetModulesListsTheDebuggee)
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

        const auto modules = session->get_modules();
        ASSERT_GT(modules.size(), 0u) << "Expected at least the debuggee module after launch.";
        bool found_debuggee = false;
        for (const auto &module : modules)
        {
            EXPECT_FALSE(module.name.empty()) << "Every module should carry a name.";
            EXPECT_NE(module.base, 0u) << "Every module should carry a base address.";
            if (module.image_path.find("test_launch") != std::string::npos)
            {
                found_debuggee = true;
            }
        }
        EXPECT_TRUE(found_debuggee) << "Expected the launched debuggee among the modules.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, ReadAndWriteMemoryRoundTripsAtALocalsAddress)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_struct_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_struct_1.exe not found (build test-targets/testapp).");
    const std::string source = resolve_struct_target_source();
    DAP_REQUIRE_OR_SKIP(source, "test-targets/testapp/struct-1.cpp not found.");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);
        session->set_symbol_path({resolve_struct_target_directory()});
        session->set_source_path({fs::path(source).parent_path().string()});
        session->enable_source_line_support();
        session->set_source_breakpoints(source, std::vector<int>{37});
        session->continue_();
        session->wait_for_event(10000);

        // p is point2{3, 7}: its address must read back 3 as the first int.
        const auto [address, size] = session->get_symbol_address(0, "p");
        ASSERT_NE(address, 0u);
        ASSERT_EQ(size, 8u) << "point2 is two ints.";

        const auto bytes = session->read_memory(address, 8);
        ASSERT_EQ(bytes.size(), 8u);
        EXPECT_EQ(bytes[0], 3u) << "p.x is 3.";
        EXPECT_EQ(bytes[4], 7u) << "p.y is 7.";

        // Overwrite p.x with 42 and read it back through the locals tree.
        const std::vector<unsigned char> new_x = {42, 0, 0, 0};
        EXPECT_EQ(session->write_memory(address, new_x), 4u);
        const auto after = session->read_memory(address, 4);
        ASSERT_EQ(after.size(), 4u);
        EXPECT_EQ(after[0], 42u) << "p.x should reflect the memory write.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, FunctionBreakpointStopsInMain)
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
        session->set_symbol_path({resolve_launch_target_directory()});

        const auto results = session->set_function_breakpoints({"test_launch!main"});
        ASSERT_EQ(results.size(), 1u);
        EXPECT_TRUE(results[0].verified) << results[0].message.value_or("");

        session->continue_();
        session->wait_for_event(10000);

        const auto frames = session->get_stack_trace();
        ASSERT_GT(frames.size(), 0u);
        ASSERT_TRUE(frames[0].name.has_value());
        EXPECT_NE(frames[0].name->find("main"), std::string::npos)
            << "Expected to stop in main, got: " << *frames[0].name;

        // Replace-all: an empty set clears the function breakpoint.
        EXPECT_TRUE(session->set_function_breakpoints({}).empty());
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, InstructionBreakpointStopsAtAddress)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_struct_target_path();
    DAP_REQUIRE_OR_SKIP(target, "test_struct_1.exe not found (build test-targets/testapp).");
    const std::string source = resolve_struct_target_source();
    DAP_REQUIRE_OR_SKIP(source, "test-targets/testapp/struct-1.cpp not found.");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);
        session->set_symbol_path({resolve_struct_target_directory()});
        session->set_source_path({fs::path(source).parent_path().string()});
        session->enable_source_line_support();
        session->set_source_breakpoints(source, std::vector<int>{37});
        session->continue_();
        session->wait_for_event(10000);

        // Break at the second instruction after the current one; it executes
        // next when this straight-line code continues.
        const auto frames = session->get_stack_trace(1);
        ASSERT_GT(frames.size(), 0u);
        const auto instructions = session->disassemble(frames[0].instruction_offset, 0, 3, false);
        ASSERT_GE(instructions.size(), 2u);
        const std::uint64_t stop_address = instructions[1].address;

        const auto results = session->set_instruction_breakpoints({stop_address});
        ASSERT_EQ(results.size(), 1u);
        EXPECT_TRUE(results[0].verified) << results[0].message.value_or("");

        session->continue_();
        session->wait_for_event(10000);

        const auto stopped_frames = session->get_stack_trace(1);
        ASSERT_GT(stopped_frames.size(), 0u);
        EXPECT_EQ(stopped_frames[0].instruction_offset, stop_address)
            << "Expected to stop exactly at the instruction breakpoint.";
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, CppFirstChanceFilterStopsOnCaughtThrow)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_test_target_path("test_exception_1.exe");
    DAP_REQUIRE_OR_SKIP(target, "test_exception_1.exe not found (build test-targets/testapp).");

    std::unique_ptr<debugger_session> session;
    try
    {
        session = std::make_unique<debugger_session>(dbgeng);
        session->launch(target);
        session->set_cpp_first_chance_break(true);
        session->continue_();
        session->wait_for_event(10000);

        const auto exception = session->get_last_exception();
        ASSERT_TRUE(exception.has_value()) << "Expected a first-chance C++ exception stop.";
        EXPECT_EQ(exception->code, 0xE06D7363u) << "Expected the MSVC C++ EH exception code.";
        EXPECT_TRUE(exception->first_chance);
        EXPECT_NE(exception->address, 0u);
    }
    catch (...)
    {
        cleanup_launched_session(session);
        throw;
    }
    cleanup_launched_session(session);
}

TEST(DebuggerSessionIntegration, DataBreakpointStopsOnWatchedWrite)
{
    const std::string dbgeng = resolve_dbgeng_path();
    DAP_REQUIRE_OR_SKIP(dbgeng, "dbgeng.dll not found (set DAP_DBGENG_WINDBG_PATH).");
    const std::string target = resolve_test_target_path("test_data_1.exe");
    DAP_REQUIRE_OR_SKIP(target, "test_data_1.exe not found (build test-targets/testapp).");
    const std::string source = resolve_test_target_source("data-1.cpp");
    DAP_REQUIRE_OR_SKIP(source, "test-targets/testapp/data-1.cpp not found.");

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
        session->set_symbol_path({fs::path(target).parent_path().string()});
        session->set_source_path({fs::path(source).parent_path().string()});
        session->enable_source_line_support();
        // Line 15 prints "data-1 armed"; watched/next are initialized there.
        session->set_source_breakpoints(source, std::vector<int>{15});
        session->continue_();
        session->wait_for_event(10000);

        const auto [address, size] = session->get_symbol_address(0, "watched");
        ASSERT_NE(address, 0u);
        ASSERT_EQ(size, 4u) << "watched is an int.";

        const auto results = session->set_data_breakpoints({data_breakpoint_spec{address, 4, false}});
        ASSERT_EQ(results.size(), 1u);
        EXPECT_TRUE(results[0].verified) << results[0].message.value_or("");

        // Continue: line 17 writes watched = next * 2 = 4 and the watchpoint fires.
        session->continue_();
        session->wait_for_event(10000);

        const auto locals = session->get_locals_tree(0);
        const variable_node *watched = find_child(locals, "watched");
        ASSERT_NE(watched, nullptr);
        EXPECT_EQ(watched->value, "4") << "The data breakpoint should fire right after the write.";

        // Clear the watchpoint before detaching: the watched stack slot is
        // reused after main returns, and a leftover hardware breakpoint in the
        // detached process re-fires forever so test_data_1 never exits.
        EXPECT_TRUE(session->set_data_breakpoints({}).empty());
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
