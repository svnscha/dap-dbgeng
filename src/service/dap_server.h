#pragma once

#include "debugger/debugger_session.h"
#include "transport/dap_message_writer.h"
#include "util/debugger_session_dispatcher.h"

namespace dap_dbgeng::service
{
// Hosts DAP request processing and maps failures to protocol error responses.
// Provides the dispatch core, the 18 request handlers, and live-session wiring.
// Requests are dispatched through the generated protocol::dap_service base;
// outbound messages go through the injected transport::dap_message_writer,
// whose single FIFO writer thread gives total wire order (so this server just
// calls writer_->send(...) in call order).
//
// Threading: handlers run on the main/transport thread. Every DebuggerSession
// call is marshaled onto a single dbgeng worker thread via dispatcher_.invoke().
// The one exception is session.interrupt() (pause/disconnect break-in), the only
// cross-thread-safe COM call, made off-dispatcher because the dispatcher thread
// is blocked inside wait_for_event. Stop/continue is event-driven: continue/step/
// configurationDone start a background wait loop whose callbacks (firing on the
// dispatcher thread inside wait_for_event) set pending-stopped / push DAP events.
class dap_server : public protocol::dap_service
{
  public:
    explicit dap_server(transport::dap_message_writer &writer);
    ~dap_server() override;

    // Dispatch one inbound DAP message. Unsupported/failed requests are turned
    // into DAP error responses. Returns true when the server should exit.
    bool handle_request(const nlohmann::json &root);

    // Resolve dbgeng.dll: an explicit `requested` path if it exists, else a
    // dbgeng.dll bundled next to the adapter, else the installed Windows SDK
    // Debugging Tools. Returns false (and clears engine_path) when none is found.
    static bool try_resolve_debugger_engine_path(const std::optional<std::string> &requested, std::string &engine_path);

  protected:
    // The 18 implemented request handlers (each in its own dap_server_<cmd>.cpp).
    void handle_initialize_request(const protocol::InitializeRequest &request) override;
    void handle_launch_request(const protocol::LaunchRequest &request) override;
    void handle_attach_request(const protocol::AttachRequest &request) override;
    void handle_configuration_done_request(const protocol::ConfigurationDoneRequest &request) override;
    void handle_threads_request(const protocol::ThreadsRequest &request) override;
    void handle_stack_trace_request(const protocol::StackTraceRequest &request) override;
    void handle_scopes_request(const protocol::ScopesRequest &request) override;
    void handle_variables_request(const protocol::VariablesRequest &request) override;
    void handle_set_variable_request(const protocol::SetVariableRequest &request) override;
    void handle_set_breakpoints_request(const protocol::SetBreakpointsRequest &request) override;
    void handle_continue_request(const protocol::ContinueRequest &request) override;
    void handle_pause_request(const protocol::PauseRequest &request) override;
    void handle_next_request(const protocol::NextRequest &request) override;
    void handle_step_in_request(const protocol::StepInRequest &request) override;
    void handle_step_out_request(const protocol::StepOutRequest &request) override;
    void handle_terminate_request(const protocol::TerminateRequest &request) override;
    void handle_evaluate_request(const protocol::EvaluateRequest &request) override;
    void handle_disassemble_request(const protocol::DisassembleRequest &request) override;
    void handle_disconnect_request(const protocol::DisconnectRequest &request) override;
    void handle_source_request(const protocol::SourceRequest &request) override;

  public:
    // ---- Test seams ---------------------------------------------------------
    // Pure stackTrace-window helpers used by both the handler and the gtests.
    struct stack_trace_window
    {
        int start_frame = 0;
        int levels = 0;
    };
    struct stack_trace_result
    {
        std::vector<debugger::stack_frame_info> frames;
        int total_frames = 0;
    };
    static stack_trace_window read_stack_trace_window(const protocol::StackTraceArguments &arguments);
    static std::vector<debugger::stack_frame_info> apply_stack_trace_window(
        const std::vector<debugger::stack_frame_info> &frames, const stack_trace_window &window);
    static stack_trace_result create_stack_trace_result(const std::vector<debugger::stack_frame_info> &all_frames,
                                                        int total_frames, const stack_trace_window &window);
    static int get_stack_trace_fetch_limit(const stack_trace_window &window);
    static std::string create_evaluate_command(const std::string &expression,
                                               std::optional<std::uint32_t> frame_number);

    // Test-only state pokes.
    void set_execution_running_for_test(bool value)
    {
        is_execution_running_.store(value);
    }
    void register_frames_for_test(int thread_id, const std::vector<debugger::stack_frame_info> &frames)
    {
        register_frames(thread_id, frames);
    }
    bool has_frame_context_for_test(int frame_id) const
    {
        return frame_contexts_.contains(frame_id);
    }
    std::size_t frame_context_count_for_test() const
    {
        return frame_contexts_.size();
    }
    void register_locals_container_for_test(int variables_reference, const std::vector<protocol::Variable> &variables);
    // Runs build_variable_tree for a node, allocating real child containers in the
    // reference map so a follow-up variables request can resolve them.
    protocol::Variable build_variable_tree_for_test(const debugger::variable_node &node)
    {
        return build_variable_tree(node, frame_context{}, std::string{});
    }

  private:
    // ---- Outbound ------------------------------------------------------------
    void send(nlohmann::json message);

    template <class TResponse> void send_response(int request_seq, const std::string &command, TResponse response)
    {
        response.request_seq = request_seq;
        response.success = true;
        response.command = command;
        send(nlohmann::json(response));
    }
    void send_error_response(int request_seq, const std::string &command, const std::string &message);

    // Execution-request argument validation. Each returns true when the argument
    // is acceptable, or sends the matching error response and returns false. The
    // adapter advertises neither single-thread execution nor stepIn targets.
    bool require_positive_thread_id(int request_seq, const std::string &command, int thread_id);
    bool reject_single_thread(int request_seq, const std::string &command, bool single_thread);
    bool reject_target_id(int request_seq, const std::string &command, bool has_target_id);

    // Request-driven process/stopped event helpers.
    void send_process_event(const std::string &name, std::optional<int> process_id,
                            protocol::ProcessEventBodyStartMethod start_method);
    void send_stopped_event(protocol::StoppedEventBodyReason reason, std::optional<std::string> description);

    // ---- Session lifecycle ---------------------------------------------------
    struct session_configuration
    {
        std::optional<std::vector<std::string>> symbol_path;
        std::optional<std::vector<std::string>> source_path;
    };
    debugger::debugger_session &create_debugger_session(const std::string &engine_path);
    void dispose_debugger_session();
    struct disconnect_request_options
    {
        std::optional<bool> restart;
        std::optional<bool> terminate_debuggee;
        std::optional<bool> suspend_debuggee;
    };
    void disconnect_debugger_session(const disconnect_request_options &options);
    void disconnect_kernel_leaving_machine_running(debugger::debugger_session &session);
    debugger::debugger_session &require_debugger_session();
    void apply_session_configuration(debugger::debugger_session &session, const session_configuration &configuration);
    void wire_debugger_session(debugger::debugger_session &session);

    // ---- Threads / current thread -------------------------------------------
    std::vector<protocol::Thread> get_threads();
    std::optional<int> try_get_current_thread_id();

    // Run `reader` against the active session, returning `fallback` if there is no
    // session or the session is no longer available.
    template <class Reader, class T> T read_session_data_or_default(Reader &&reader, T fallback)
    {
        // Capture the session pointer once into a local before the dispatcher
        // call: the member can be nulled concurrently when the session ends
        // mid-flight, but the engine object stays alive (parked, drained off the
        // callback) until this read has run.
        debugger::debugger_session *session = debugger_session_.get();
        if (session == nullptr)
        {
            return fallback;
        }
        try
        {
            return reader(*session);
        }
        catch (const std::exception &)
        {
            // When the session became unavailable, a failed read simply yields
            // the fallback.
            if (is_session_unavailable())
            {
                return fallback;
            }
            throw;
        }
    }
    bool is_session_unavailable() const;

    // ---- Pending-stopped gate -----------------------------------------------
    struct pending_stopped_event
    {
        protocol::StoppedEventBodyReason reason = protocol::StoppedEventBodyReason::Step;
        std::optional<std::string> description;
        std::optional<std::vector<int>> hit_breakpoint_ids;
    };
    void set_pending_stopped_event(protocol::StoppedEventBodyReason reason, std::optional<std::string> description,
                                   std::optional<std::vector<int>> hit_breakpoint_ids = std::nullopt);
    void clear_pending_stopped_event();
    bool has_pending_stopped_event();
    bool is_represented_stop_while_halted() const;

    // For a live user-mode target, setBreakpoints briefly interrupts to a clean
    // stop (events suppressed), applies the breakpoints, and resumes.
    std::vector<debugger::source_breakpoint_result> set_breakpoints_while_running(
        debugger::debugger_session &session, const std::string &source_path,
        const std::vector<debugger::source_breakpoint_spec> &specs);

    // ---- Stepping / suspended state -----------------------------------------
    void prepare_stepping_granularity(debugger::debugger_session &session,
                                      std::optional<protocol::SteppingGranularity> granularity);
    void restore_source_line_stepping_if_needed(debugger::debugger_session &session);
    void reset_launch_configuration_state();
    void reset_suspended_state();
    void continue_after_configuration_done();

    // ---- Frame / variable context maps --------------------------------------
    enum class variable_container_kind
    {
        locals,
        registers,
        structure
    };
    struct frame_context
    {
        int thread_id = 0;
        std::uint32_t frame_number = 0;
        std::optional<debugger::source_location> source;
    };
    struct variable_container_context
    {
        int thread_id = 0;
        std::uint32_t frame_number = 0;
        variable_container_kind kind = variable_container_kind::locals;
        std::vector<protocol::Variable> variables;
    };
    void register_frames(int thread_id, const std::vector<debugger::stack_frame_info> &frames);
    int create_variables_reference(const frame_context &context, variable_container_kind kind,
                                   const std::vector<protocol::Variable> &variables);
    static protocol::Variable create_variable(const debugger::named_value_info &value);
    // Converts a local-variable tree node into a protocol::Variable, allocating a
    // variablesReference (and its child container) for any node with children so
    // structs expand. evaluate_name is the full access path (e.g. "t.origin.x").
    protocol::Variable build_variable_tree(const debugger::variable_node &node, const frame_context &context,
                                           const std::string &parent_evaluate_name);
    // Assigns one field of a struct-kind variable container by its full access
    // path (the child's evaluate_name), via the debugger's native symbol-group
    // write, then refreshes that child in the cached container.
    void handle_set_struct_field(const protocol::SetVariableRequest &request,
                                 const variable_container_context &container, int variables_reference,
                                 const std::string &name, const std::string &value);
    static protocol::Scope create_scope(const std::string &name, protocol::ScopePresentationHint presentation_hint,
                                        int variables_reference, int named_variables,
                                        const std::optional<debugger::source_location> &source);

    // ---- setBreakpoints helpers ---------------------------------------------
    static protocol::Breakpoint create_source_breakpoint_response(const protocol::Source &source,
                                                                  const protocol::SourceBreakpoint &breakpoint,
                                                                  bool verified, std::optional<std::string> message,
                                                                  std::optional<protocol::BreakpointReason> reason,
                                                                  std::optional<int> id);
    void update_conditional_breakpoint_state(const std::string &source_path,
                                             const std::vector<debugger::source_breakpoint_result> &configured,
                                             const std::vector<protocol::SourceBreakpoint> &requested);
    void clear_conditional_breakpoint_state(const std::string &source_path);

    // ---- Conditional / kernel auto-continue (run on the dispatcher thread) ---
    bool try_auto_continue_conditional_breakpoint_hit(debugger::debugger_session &session, std::uint32_t breakpoint_id);
    bool try_auto_continue_kernel_initial_break(debugger::debugger_session &session);
    static std::optional<bool> try_evaluate_conditional_breakpoint(debugger::debugger_session &session,
                                                                   const std::string &condition);

    // ---- Suppressed-events scope --------------------------------------------
    template <class Action> void run_with_suppressed_session_events(Action &&action)
    {
        suppress_session_events_.store(true);
        try
        {
            action();
        }
        catch (...)
        {
            suppress_session_events_.store(false);
            throw;
        }
        suppress_session_events_.store(false);
    }

    // ---- Callback-driven event queueing (run on the dispatcher thread) -------
    void queue_thread_event(protocol::ThreadEventBodyReason reason);
    void queue_session_termination(int exit_code);
    void queue_session_ended();
    void flush_pending_thread_events();
    void queue_session_event(nlohmann::json message);

    // ---- Wait-for-session-event loop ----------------------------------------
    void start_waiting_for_session_event(debugger::debugger_session &session);
    void wait_for_session_event_loop(debugger::debugger_session *session);
    void drain_pending_disposed_session();
    void join_wait_loop();

    // ---- State ---------------------------------------------------------------
    transport::dap_message_writer &writer_;
    util::debugger_session_dispatcher dispatcher_;

    std::unique_ptr<debugger::debugger_session> debugger_session_;
    // When the engine reports session-end synchronously inside its own event
    // callback (on the dispatcher thread), the session must not be destroyed on
    // that callback stack — tearing the engine down re-entrantly corrupts it and
    // drops the in-flight terminate/disconnect response. Instead the owning
    // unique_ptr is parked here and reset off the callback stack (by the wait
    // loop, after wait_for_event unwinds), so the real COM teardown happens
    // later off the callback.
    std::unique_ptr<debugger::debugger_session> pending_disposed_session_;
    nlohmann::json current_request_json_;
    bool has_current_request_json_ = false;

    // Execution-state flags: written on the dbgeng dispatcher thread (event
    // callbacks) and read on the request-handler thread; atomic to avoid stale reads.
    std::atomic<bool> is_execution_running_{false};
    std::atomic<bool> session_stop_or_exit_observed_{false};
    std::atomic<bool> suppress_session_events_{false};
    std::atomic<bool> kernel_initial_resume_pending_{false};
    std::atomic<bool> skip_next_break_hit_event_{false};

    bool detach_on_disconnect_ = false;
    bool terminate_debuggee_on_disconnect_ = false;
    bool launch_awaiting_configuration_done_ = false;
    bool launch_stop_at_entry_ = false;
    std::optional<int> launch_thread_id_;
    bool restore_source_line_stepping_after_stop_ = false;
    bool should_exit_ = false;
    bool initialize_request_handled_ = false;

    std::mutex pending_stopped_gate_;
    std::optional<pending_stopped_event> pending_stopped_event_;

    std::map<int, frame_context> frame_contexts_;
    int next_variables_reference_ = 1;
    std::map<int, variable_container_context> variables_by_reference_;

    // Conditional-breakpoint condition by engine id, and ids per source path
    // (case-insensitive key) so a path's previous conditions clear on replacement.
    std::map<int, std::string> conditional_breakpoint_conditions_by_id_;
    std::map<std::string, std::vector<int>> conditional_breakpoint_ids_by_path_;

    std::mutex pending_thread_events_gate_;
    std::deque<nlohmann::json> pending_thread_events_;

    std::mutex session_event_wait_gate_;
    std::thread wait_for_session_event_thread_;
    bool wait_loop_active_ = false;
};
} // namespace dap_dbgeng::service
