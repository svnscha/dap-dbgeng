// In-process gtests for the DAP request handlers: the pure validation / error
// paths that need no live engine. Live-session cases are covered separately
// (skippable) and by replay.
#include <gtest/gtest.h>

#include "service/dap_server.h"
#include "transport/dap_message_writer.h"

namespace
{
using dap_dbgeng::service::dap_server;

// Buffers every message the server sends, in order, with small lookup helpers.
// Synchronous: handlers run inline.
class recording_message_writer : public dap_dbgeng::transport::dap_message_writer
{
  public:
    std::vector<nlohmann::json> messages;

    void send(nlohmann::json message) override
    {
        messages.push_back(std::move(message));
    }

    // First message matching command == `command`, or nullptr.
    const nlohmann::json *find_response(const std::string &command, int request_seq) const
    {
        for (const auto &message : messages)
        {
            if (message.value("type", std::string{}) == "response" &&
                message.value("command", std::string{}) == command && message.value("request_seq", -1) == request_seq)
            {
                return &message;
            }
        }
        return nullptr;
    }
};

// Build a request json from command + arguments and submit it.
nlohmann::json make_request(int seq, const std::string &command, nlohmann::json arguments)
{
    return nlohmann::json{{"seq", seq}, {"type", "request"}, {"command", command}, {"arguments", std::move(arguments)}};
}

std::string error_format(const nlohmann::json &response)
{
    return response.at("body").at("error").at("format").get<std::string>();
}

// --- initialize --------------------------------------------------------------

TEST(DapServerHandlers, InitializeAdvertisesImplementedCapabilities)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "initialize", {{"adapterID", "dap-dbgeng-tests"}}));

    const nlohmann::json *response = writer.find_response("initialize", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_TRUE(response->at("success").get<bool>());
    const nlohmann::json &body = response->at("body");
    EXPECT_TRUE(body.at("supportsSetVariable").get<bool>());
    EXPECT_FALSE(body.at("supportsFunctionBreakpoints").get<bool>());
    EXPECT_TRUE(body.at("supportsConditionalBreakpoints").get<bool>());
    EXPECT_FALSE(body.at("supportsReadMemoryRequest").get<bool>());
    EXPECT_FALSE(body.at("supportsWriteMemoryRequest").get<bool>());
    EXPECT_FALSE(body.at("supportsModulesRequest").get<bool>());
    EXPECT_FALSE(body.at("supportsRestartRequest").get<bool>());
    EXPECT_TRUE(body.at("supportsDisassembleRequest").get<bool>());
    EXPECT_FALSE(body.at("supportsEvaluateForHovers").get<bool>());
    EXPECT_FALSE(body.at("supportsStepBack").get<bool>());
    EXPECT_FALSE(body.at("supportsValueFormattingOptions").get<bool>());
    EXPECT_TRUE(body.at("supportsClipboardContext").get<bool>());
    EXPECT_TRUE(body.at("supportsSteppingGranularity").get<bool>());
    EXPECT_TRUE(body.at("supportsTerminateRequest").get<bool>());
    EXPECT_TRUE(body.at("supportTerminateDebuggee").get<bool>());

    // initialized event follows the response on the same ordered path.
    ASSERT_GE(writer.messages.size(), 2u);
    EXPECT_EQ(writer.messages.back().at("event"), "initialized");
}

// --- default handler / unsupported requests ---------------------------------

TEST(DapServerHandlers, ModulesReturnsUnsupportedFromDefaultHandler)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "modules", {{"startModule", 0}, {"moduleCount", 20}}));

    const nlohmann::json *response = writer.find_response("modules", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_FALSE(response->at("success").get<bool>());
    EXPECT_EQ(error_format(*response), "Unsupported command: <modules>");
}

// --- continue ----------------------------------------------------------------

TEST(DapServerHandlers, ContinueWithNonPositiveThreadIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "continue", {{"threadId", 0}}));

    const nlohmann::json *response = writer.find_response("continue", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_FALSE(response->at("success").get<bool>());
    EXPECT_EQ(error_format(*response), "The continue request requires a positive threadId.");
}

TEST(DapServerHandlers, ContinueWithSingleThreadReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "continue", {{"threadId", 1}, {"singleThread", true}}));

    const nlohmann::json *response = writer.find_response("continue", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response),
              "The continue request does not support singleThread because supportsSingleThreadExecutionRequests is "
              "false.");
}

// --- next / stepIn / stepOut -------------------------------------------------

TEST(DapServerHandlers, NextWithNonPositiveThreadIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "next", {{"threadId", 0}}));

    const nlohmann::json *response = writer.find_response("next", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The next request requires a positive threadId.");
}

TEST(DapServerHandlers, NextWithSingleThreadReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "next", {{"threadId", 1}, {"singleThread", true}}));

    const nlohmann::json *response = writer.find_response("next", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response),
              "The next request does not support singleThread because supportsSingleThreadExecutionRequests is false.");
}

TEST(DapServerHandlers, StepInWithNonPositiveThreadIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "stepIn", {{"threadId", 0}}));

    const nlohmann::json *response = writer.find_response("stepIn", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The stepIn request requires a positive threadId.");
}

TEST(DapServerHandlers, StepInWithTargetIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "stepIn", {{"threadId", 1}, {"targetId", 7}}));

    const nlohmann::json *response = writer.find_response("stepIn", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response),
              "The stepIn request does not support targetId because supportsStepInTargetsRequest is false.");
}

TEST(DapServerHandlers, StepInWithSingleThreadReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "stepIn", {{"threadId", 1}, {"singleThread", true}}));

    const nlohmann::json *response = writer.find_response("stepIn", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response),
              "The stepIn request does not support singleThread because supportsSingleThreadExecutionRequests is "
              "false.");
}

TEST(DapServerHandlers, StepOutWithNonPositiveThreadIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "stepOut", {{"threadId", 0}}));

    const nlohmann::json *response = writer.find_response("stepOut", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The stepOut request requires a positive threadId.");
}

TEST(DapServerHandlers, StepOutWithSingleThreadReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "stepOut", {{"threadId", 1}, {"singleThread", true}}));

    const nlohmann::json *response = writer.find_response("stepOut", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response),
              "The stepOut request does not support singleThread because supportsSingleThreadExecutionRequests is "
              "false.");
}

// --- pause -------------------------------------------------------------------

TEST(DapServerHandlers, PauseWithNonPositiveThreadIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "pause", {{"threadId", 0}}));

    const nlohmann::json *response = writer.find_response("pause", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The pause request requires a positive threadId.");
}

TEST(DapServerHandlers, PauseWhenNotRunningReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "pause", {{"threadId", 1}}));

    const nlohmann::json *response = writer.find_response("pause", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The debuggee is not running.");
}

// --- stackTrace --------------------------------------------------------------

TEST(DapServerHandlers, StackTraceWithNonPositiveThreadIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "stackTrace", {{"threadId", 0}, {"startFrame", 0}, {"levels", 1}}));

    const nlohmann::json *response = writer.find_response("stackTrace", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The stackTrace request requires a positive threadId.");
}

// --- scopes ------------------------------------------------------------------

TEST(DapServerHandlers, ScopesWithNonPositiveFrameIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "scopes", {{"frameId", 0}}));

    const nlohmann::json *response = writer.find_response("scopes", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The scopes request requires a positive frameId.");
}

TEST(DapServerHandlers, ScopesWithUnknownFrameIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "scopes", {{"frameId", 7}}));

    const nlohmann::json *response = writer.find_response("scopes", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "Unknown frame id '7'. Request stackTrace again for the current stop.");
}

// --- variables ---------------------------------------------------------------

TEST(DapServerHandlers, VariablesWithNonPositiveReferenceReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "variables", {{"variablesReference", 0}}));

    const nlohmann::json *response = writer.find_response("variables", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The variables request requires a positive variablesReference.");
}

TEST(DapServerHandlers, VariablesWithUnknownReferenceReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "variables", {{"variablesReference", 7}}));

    const nlohmann::json *response = writer.find_response("variables", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "Unknown variablesReference '7'. Request scopes again for the current stop.");
}

TEST(DapServerHandlers, VariablesIndexedFilterOnNamedContainerReturnsEmpty)
{
    recording_message_writer writer;
    dap_server server(writer);

    dap_dbgeng::protocol::Variable alpha;
    alpha.name = "alpha";
    alpha.value = "1";
    dap_dbgeng::protocol::Variable beta;
    beta.name = "beta";
    beta.value = "2";
    server.register_locals_container_for_test(7, {alpha, beta});

    server.handle_request(make_request(1, "variables", {{"variablesReference", 7}, {"filter", "indexed"}}));

    const nlohmann::json *response = writer.find_response("variables", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_TRUE(response->at("success").get<bool>());
    EXPECT_EQ(response->at("body").at("variables").size(), 0u);
}

TEST(DapServerHandlers, VariablesHonorsStartAndCountPaging)
{
    recording_message_writer writer;
    dap_server server(writer);

    dap_dbgeng::protocol::Variable alpha;
    alpha.name = "alpha";
    dap_dbgeng::protocol::Variable beta;
    beta.name = "beta";
    dap_dbgeng::protocol::Variable gamma;
    gamma.name = "gamma";
    server.register_locals_container_for_test(7, {alpha, beta, gamma});

    server.handle_request(make_request(1, "variables", {{"variablesReference", 7}, {"start", 1}, {"count", 1}}));

    const nlohmann::json *response = writer.find_response("variables", 1);
    ASSERT_NE(response, nullptr);
    ASSERT_EQ(response->at("body").at("variables").size(), 1u);
    EXPECT_EQ(response->at("body").at("variables")[0].at("name"), "beta");
}

TEST(DapServerHandlers, ScopesBuildsExpandableNestedStructVariables)
{
    recording_message_writer writer;
    dap_server server(writer);

    // t { origin { x, y }, id } - one level of nesting plus a scalar leaf.
    dap_dbgeng::debugger::variable_node x{"x", "10", "int", false, {}};
    dap_dbgeng::debugger::variable_node y{"y", "20", "int", false, {}};
    dap_dbgeng::debugger::variable_node origin{"origin", "{...}", "point2", false, {x, y}};
    dap_dbgeng::debugger::variable_node id{"id", "42", "int", false, {}};
    dap_dbgeng::debugger::variable_node t{"t", "{...}", "transform", false, {origin, id}};

    const dap_dbgeng::protocol::Variable t_var = server.build_variable_tree_for_test(t);

    // The struct local is expandable; the scalar path is reached via expansion.
    EXPECT_EQ(t_var.name, "t");
    EXPECT_EQ(t_var.type, "transform");
    EXPECT_EQ(t_var.named_variables, 2);
    ASSERT_GT(t_var.variables_reference, 0) << "A struct local must be expandable.";

    // Expanding 't' yields its fields, with 'origin' itself expandable and 'id' a leaf.
    server.handle_request(make_request(1, "variables", {{"variablesReference", t_var.variables_reference}}));
    const nlohmann::json *t_children = writer.find_response("variables", 1);
    ASSERT_NE(t_children, nullptr);
    const nlohmann::json &fields = t_children->at("body").at("variables");
    ASSERT_EQ(fields.size(), 2u);

    int origin_ref = 0;
    for (const auto &field : fields)
    {
        if (field.at("name") == "origin")
        {
            origin_ref = field.at("variablesReference").get<int>();
            EXPECT_EQ(field.at("evaluateName"), "t.origin");
        }
        else if (field.at("name") == "id")
        {
            EXPECT_EQ(field.at("variablesReference").get<int>(), 0) << "A scalar field is not expandable.";
            EXPECT_EQ(field.at("evaluateName"), "t.id");
        }
    }
    ASSERT_GT(origin_ref, 0) << "'t.origin' is a struct and must be expandable.";

    // Second level: expanding 'origin' yields its scalar fields with full paths.
    server.handle_request(make_request(2, "variables", {{"variablesReference", origin_ref}}));
    const nlohmann::json *origin_children = writer.find_response("variables", 2);
    ASSERT_NE(origin_children, nullptr);
    const nlohmann::json &scalars = origin_children->at("body").at("variables");
    ASSERT_EQ(scalars.size(), 2u);
    EXPECT_EQ(scalars[0].at("name"), "x");
    EXPECT_EQ(scalars[0].at("evaluateName"), "t.origin.x");
    EXPECT_EQ(scalars[0].at("variablesReference").get<int>(), 0);
}

// --- setVariable -------------------------------------------------------------

TEST(DapServerHandlers, SetVariableWithNonPositiveReferenceReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(
        make_request(1, "setVariable", {{"variablesReference", 0}, {"name", "value"}, {"value", "1"}}));

    const nlohmann::json *response = writer.find_response("setVariable", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The setVariable request requires a positive variablesReference.");
}

TEST(DapServerHandlers, SetVariableWithUnknownReferenceReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(
        make_request(1, "setVariable", {{"variablesReference", 7}, {"name", "value"}, {"value", "1"}}));

    const nlohmann::json *response = writer.find_response("setVariable", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "Unknown variablesReference '7'. Request scopes again for the current stop.");
}

TEST(DapServerHandlers, SetVariableWhileRunningRejectsBeforeLookup)
{
    recording_message_writer writer;
    dap_server server(writer);
    server.set_execution_running_for_test(true);

    server.handle_request(
        make_request(1, "setVariable", {{"variablesReference", 7}, {"name", "value"}, {"value", "1"}}));

    const nlohmann::json *response = writer.find_response("setVariable", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response),
              "The debuggee is currently running. Wait until execution stops before changing variables.");
}

TEST(DapServerHandlers, SetVariableOnStructFieldContainerRoutesToSession)
{
    recording_message_writer writer;
    dap_server server(writer);

    // A struct local's child container is a 'structure' kind. Editing a field is
    // now accepted and routed to the debugger (which assigns it natively); without
    // a live session here it surfaces the no-session error rather than a rejection,
    // proving struct containers are no longer turned away by the handler itself.
    dap_dbgeng::debugger::variable_node x{"x", "10", "int", false, {}};
    dap_dbgeng::debugger::variable_node origin{"origin", "{...}", "point2", false, {x}};
    const dap_dbgeng::protocol::Variable origin_var = server.build_variable_tree_for_test(origin);
    ASSERT_GT(origin_var.variables_reference, 0);

    server.handle_request(make_request(
        1, "setVariable", {{"variablesReference", origin_var.variables_reference}, {"name", "x"}, {"value", "5"}}));

    const nlohmann::json *response = writer.find_response("setVariable", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_FALSE(response->at("success").get<bool>());
    EXPECT_EQ(error_format(*response), "No active debugger session is available.");
}

// --- setBreakpoints ----------------------------------------------------------

TEST(DapServerHandlers, SetBreakpointsWithSourceReferenceOnlyReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(
        1, "setBreakpoints",
        {{"source", {{"name", "disassembly"}, {"sourceReference", 1}}}, {"breakpoints", {{{"line", 1}}}}}));

    const nlohmann::json *response = writer.find_response("setBreakpoints", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_FALSE(response->at("success").get<bool>());
    EXPECT_NE(error_format(*response).find("source.sourceReference"), std::string::npos);
    EXPECT_NE(error_format(*response).find("source.path"), std::string::npos);
}

TEST(DapServerHandlers, SetBreakpointsWithPathAndSourceReferenceReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(
        make_request(1, "setBreakpoints",
                     {{"source", {{"name", "Program.cpp"}, {"path", "C:\\repo\\Program.cpp"}, {"sourceReference", 1}}},
                      {"breakpoints", {{{"line", 1}}}}}));

    const nlohmann::json *response = writer.find_response("setBreakpoints", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_FALSE(response->at("success").get<bool>());
    EXPECT_NE(error_format(*response).find("source.sourceReference"), std::string::npos);
}

// --- evaluate ----------------------------------------------------------------

TEST(DapServerHandlers, EvaluateWithNonPositiveFrameIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "evaluate", {{"expression", "1+1"}, {"frameId", 0}}));

    const nlohmann::json *response = writer.find_response("evaluate", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The evaluate request requires a positive frameId when frameId is provided.");
}

TEST(DapServerHandlers, EvaluateWithUnknownFrameIdReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "evaluate", {{"expression", "1+1"}, {"frameId", 7}}));

    const nlohmann::json *response = writer.find_response("evaluate", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "Unknown frame id '7'. Request stackTrace again for the current stop.");
}

TEST(DapServerHandlers, EvaluateWithLineButNoSourceReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "evaluate", {{"expression", "1+1"}, {"line", 10}}));

    const nlohmann::json *response = writer.find_response("evaluate", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The evaluate request requires source when line is provided.");
}

TEST(DapServerHandlers, EvaluateWithColumnButNoLineReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "evaluate", {{"expression", "1+1"}, {"column", 4}}));

    const nlohmann::json *response = writer.find_response("evaluate", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The evaluate request requires line when column is provided.");
}

TEST(DapServerHandlers, EvaluateWithUppercaseExecutionControlCommandReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "evaluate", {{"expression", "G"}}));

    const nlohmann::json *response = writer.find_response("evaluate", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response),
              "Execution-control WinDbg commands are not supported in evaluate. Use the DAP continue and stepping "
              "requests instead.");
}

TEST(DapServerHandlers, EvaluateWithSemicolonPrefixedExecutionControlCommandReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "evaluate", {{"expression", ".echo ready; g"}}));

    const nlohmann::json *response = writer.find_response("evaluate", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response),
              "Execution-control WinDbg commands are not supported in evaluate. Use the DAP continue and stepping "
              "requests instead.");
}

// --- disassemble -------------------------------------------------------------

TEST(DapServerHandlers, DisassembleWithoutSessionReturnsError)
{
    recording_message_writer writer;
    dap_server server(writer);

    server.handle_request(make_request(1, "disassemble",
                                       {{"memoryReference", "0x7FF65CC71591"},
                                        {"offset", 0},
                                        {"instructionOffset", -2},
                                        {"instructionCount", 4},
                                        {"resolveSymbols", true}}));

    const nlohmann::json *response = writer.find_response("disassemble", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The disassemble request requires an active debugger session.");
}

TEST(DapServerHandlers, DisassembleWhileRunningValidatesArgumentsFirst)
{
    recording_message_writer writer;
    dap_server server(writer);
    server.set_execution_running_for_test(true);

    server.handle_request(make_request(1, "disassemble", {{"memoryReference", ""}, {"instructionCount", 1}}));

    const nlohmann::json *response = writer.find_response("disassemble", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(error_format(*response), "The disassemble request requires a non-empty 'memoryReference'.");
}

// --- terminate (no session) --------------------------------------------------

TEST(DapServerHandlers, TerminateWithoutSessionRespondsAndTerminates)
{
    recording_message_writer writer;
    dap_server server(writer);

    const bool should_exit = server.handle_request(make_request(1, "terminate", nlohmann::json::object()));

    const nlohmann::json *response = writer.find_response("terminate", 1);
    ASSERT_NE(response, nullptr);
    EXPECT_TRUE(response->at("success").get<bool>());
    EXPECT_EQ(writer.messages.back().at("event"), "terminated");
    EXPECT_TRUE(should_exit);
}

// --- pure stackTrace-window helpers ------------------------------------------

TEST(DapServerStackTraceHelpers, ReadStackTraceWindowWithoutLevelsRequestsAllFrames)
{
    dap_dbgeng::protocol::StackTraceArguments arguments;
    arguments.thread_id = 1;
    arguments.start_frame = 5;

    const dap_server::stack_trace_window window = dap_server::read_stack_trace_window(arguments);
    EXPECT_EQ(window.start_frame, 5);
    EXPECT_EQ(window.levels, 0);
}

TEST(DapServerStackTraceHelpers, ApplyStackTraceWindowWithoutLevelsReturnsAllFramesAfterStart)
{
    std::vector<dap_dbgeng::debugger::stack_frame_info> frames;
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        dap_dbgeng::debugger::stack_frame_info frame;
        frame.frame_number = i;
        frame.instruction_offset = 0x1000 + i;
        frames.push_back(frame);
    }

    const auto result = dap_server::apply_stack_trace_window(frames, dap_server::stack_trace_window{2, 0});
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].frame_number, 2u);
    EXPECT_EQ(result[1].frame_number, 3u);
}

TEST(DapServerStackTraceHelpers, CreateStackTraceResultPreservesTotalFrameCount)
{
    std::vector<dap_dbgeng::debugger::stack_frame_info> frames;
    for (std::uint32_t i = 0; i < 5; ++i)
    {
        dap_dbgeng::debugger::stack_frame_info frame;
        frame.frame_number = i;
        frames.push_back(frame);
    }

    const auto result = dap_server::create_stack_trace_result(frames, static_cast<int>(frames.size()),
                                                              dap_server::stack_trace_window{2, 2});
    EXPECT_EQ(result.total_frames, 5);
    ASSERT_EQ(result.frames.size(), 2u);
    EXPECT_EQ(result.frames[0].frame_number, 2u);
    EXPECT_EQ(result.frames[1].frame_number, 3u);
}

TEST(DapServerStackTraceHelpers, GetStackTraceFetchLimit)
{
    EXPECT_EQ(dap_server::get_stack_trace_fetch_limit(dap_server::stack_trace_window{2, 3}), 5);
    EXPECT_EQ(dap_server::get_stack_trace_fetch_limit(dap_server::stack_trace_window{2, 0}),
              std::numeric_limits<int>::max());
}

// --- evaluate command builder ------------------------------------------------

TEST(DapServerEvaluateHelpers, CreateEvaluateCommandWithFrameNumberPrefixesFrameSelector)
{
    EXPECT_EQ(dap_server::create_evaluate_command("dv", 3u), ".frame 3;dv");
}

TEST(DapServerEvaluateHelpers, CreateEvaluateCommandWithoutFrameNumberLeavesExpressionUnchanged)
{
    EXPECT_EQ(dap_server::create_evaluate_command("dv", std::nullopt), "dv");
}

// --- frame-id stability ------------------------------------------------------

TEST(DapServerFrameContexts, EarlierFrameIdRemainsValidWithinSameStop)
{
    recording_message_writer writer;
    dap_server server(writer);

    const int thread_id = 42752;
    dap_dbgeng::debugger::stack_frame_info first;
    first.frame_number = 0;
    first.instruction_offset = 0x1000;
    first.name = "top";
    first.source = dap_dbgeng::debugger::source_location{"C:\\src\\top.cpp", 10};
    dap_dbgeng::debugger::stack_frame_info second;
    second.frame_number = 1;
    second.instruction_offset = 0x2000;
    second.name = "next";
    second.source = dap_dbgeng::debugger::source_location{"C:\\src\\next.cpp", 20};

    server.register_frames_for_test(thread_id, {first});
    EXPECT_EQ(server.frame_context_count_for_test(), 1u);

    server.register_frames_for_test(thread_id, {second});
    // A later stackTrace page must not invalidate an earlier frame id within the same stop.
    EXPECT_EQ(server.frame_context_count_for_test(), 2u);
}
} // namespace
