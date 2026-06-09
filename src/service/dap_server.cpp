#include "service/dap_server.h"

#include "util/string_utils.h"

#include "util/dap_argument_reader.h"
#include "util/dap_message_inspector.h"
#include "util/debugger_session_dap_factory.h"

namespace dap_dbgeng::service
{
namespace factory = util::debugger_session_dap_factory;

namespace
{
constexpr int kKernelResumeSettleMilliseconds = 400;

bool path_exists(const std::string &candidate)
{
    std::error_code ec;
    return std::filesystem::exists(candidate, ec);
}

std::string dbgeng_architecture_directory_name()
{
#if defined(_M_ARM64)
    return "arm64";
#elif defined(_M_X64)
    return "amd64";
#elif defined(_M_IX86)
    return "x86";
#else
    return "amd64";
#endif
}

std::string executable_directory()
{
    char buffer[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0)
    {
        return std::string{};
    }
    return std::filesystem::path(std::string(buffer, length)).parent_path().string();
}

std::optional<std::string> env_var(const char *name)
{
    char *buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr)
    {
        return std::nullopt;
    }
    std::string value(buffer);
    free(buffer);
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

// The Windows SDK lays Debugging Tools out under Debuggers\<x64|arm64|x86>.
std::string sdk_debugger_architecture()
{
#if defined(_M_ARM64)
    return "arm64";
#elif defined(_M_X64)
    return "x64";
#elif defined(_M_IX86)
    return "x86";
#else
    return "x64";
#endif
}

// Well-known dbgeng.dll locations to try when the configuration omits dbgengPath:
// the installed Windows SDK Debugging Tools under the Program Files variants.
std::vector<std::string> default_dbgeng_candidates()
{
    const std::string suffix = "\\Windows Kits\\10\\Debuggers\\" + sdk_debugger_architecture() + "\\dbgeng.dll";
    std::vector<std::string> candidates;
    for (const char *variable : {"ProgramFiles(x86)", "ProgramW6432", "ProgramFiles"})
    {
        if (auto program_files = env_var(variable))
        {
            candidates.push_back(*program_files + suffix);
        }
    }
    candidates.push_back("C:\\Program Files (x86)" + suffix);
    return candidates;
}
} // namespace

dap_server::dap_server(transport::dap_message_writer &writer) : writer_(writer)
{
}

dap_server::~dap_server()
{
    dispose_debugger_session();
    join_wait_loop();
}

bool dap_server::handle_request(const nlohmann::json &root)
{
    should_exit_ = false;
    current_request_json_ = root;
    has_current_request_json_ = true;

    struct scope_guard
    {
        dap_server *self;
        ~scope_guard()
        {
            self->has_current_request_json_ = false;
        }
    } guard{this};

    try
    {
        dap_service::handle(root);
    }
    catch (const protocol::unhandled_dap_request &exception)
    {
        send_error_response(exception.request_seq, exception.command,
                            fmt::format("Unsupported command: <{}>", exception.command));
    }
    catch (const std::exception &exception)
    {
        const std::optional<std::string> command = util::dap_message_inspector::try_get_command(root);
        spdlog::error("Failed to handle DAP command '{}': {}", command.value_or("<unknown>"), exception.what());

        const std::optional<int> request_seq = util::dap_message_inspector::try_get_sequence(root);
        if (request_seq.has_value() && command.has_value() && !command->empty() && *command != "<unknown>")
        {
            try
            {
                send_error_response(*request_seq, *command, fmt::format("Failed to handle command: {}", *command));
            }
            catch (const std::exception &response_exception)
            {
                spdlog::error("Failed to send DAP error response for '{}': {}", *command, response_exception.what());
            }
        }
    }

    return should_exit_;
}

// ---- Outbound ---------------------------------------------------------------

void dap_server::send(nlohmann::json message)
{
    writer_.send(std::move(message));
}

void dap_server::send_error_response(int request_seq, const std::string &command, const std::string &message)
{
    protocol::Message error;
    error.id = 1;
    error.format = message;
    error.show_user = true;

    protocol::ErrorResponse response;
    response.request_seq = request_seq;
    response.success = false;
    response.command = command;
    response.body.error = std::move(error);

    send(nlohmann::json(response));
}

bool dap_server::require_positive_thread_id(int request_seq, const std::string &command, int thread_id)
{
    if (thread_id > 0)
    {
        return true;
    }
    send_error_response(request_seq, command, fmt::format("The {} request requires a positive threadId.", command));
    return false;
}

bool dap_server::reject_single_thread(int request_seq, const std::string &command, bool single_thread)
{
    if (!single_thread)
    {
        return true;
    }
    send_error_response(request_seq, command,
                        fmt::format("The {} request does not support singleThread because "
                                    "supportsSingleThreadExecutionRequests is false.",
                                    command));
    return false;
}

bool dap_server::reject_target_id(int request_seq, const std::string &command, bool has_target_id)
{
    if (!has_target_id)
    {
        return true;
    }
    send_error_response(
        request_seq, command,
        fmt::format("The {} request does not support targetId because supportsStepInTargetsRequest is false.",
                    command));
    return false;
}

void dap_server::send_process_event(const std::string &name, std::optional<int> process_id,
                                    protocol::ProcessEventBodyStartMethod start_method)
{
    send(nlohmann::json(factory::create_process_event(name, process_id, start_method)));
}

void dap_server::send_stopped_event(protocol::StoppedEventBodyReason reason, std::optional<std::string> description)
{
    send(nlohmann::json(factory::create_stopped_event(reason, std::move(description), try_get_current_thread_id())));
}

// ---- Session lifecycle ------------------------------------------------------

debugger::debugger_session &dap_server::create_debugger_session(const std::string &engine_path)
{
    dispose_debugger_session();
    debugger_session_ = dispatcher_.invoke([&]() -> std::unique_ptr<debugger::debugger_session> {
        return std::make_unique<debugger::debugger_session>(engine_path);
    });
    wire_debugger_session(*debugger_session_);
    return *debugger_session_;
}

bool dap_server::try_resolve_debugger_engine_path(const std::optional<std::string> &requested, std::string &engine_path)
{
    engine_path.clear();
    if (requested.has_value() && !util::is_blank(*requested))
    {
        if (path_exists(*requested))
        {
            engine_path = *requested;
            return true;
        }
        return false;
    }

    // No explicit path: prefer a dbgeng.dll bundled next to the adapter, then fall
    // back to the installed Windows SDK Debugging Tools.
    const std::string base = executable_directory();
    if (!base.empty())
    {
        const std::string bundled =
            (std::filesystem::path(base) / dbgeng_architecture_directory_name() / "dbgeng.dll").string();
        if (path_exists(bundled))
        {
            engine_path = bundled;
            return true;
        }
    }

    for (const std::string &candidate : default_dbgeng_candidates())
    {
        if (path_exists(candidate))
        {
            engine_path = candidate;
            return true;
        }
    }
    return false;
}

void dap_server::dispose_debugger_session()
{
    if (debugger_session_ != nullptr)
    {
        std::unique_ptr<debugger::debugger_session> owned = std::move(debugger_session_);

        if (dispatcher_.is_on_worker_thread())
        {
            // Re-entrant teardown: the engine reported session-end synchronously
            // inside its own event callback (we are on the dispatcher thread,
            // inside wait_for_event). Destroying the engine here would re-enter it
            // mid-callback (SetEventCallbacks(nullptr)/EndSession/FreeLibrary) and
            // corrupt it, dropping the in-flight response. Park the session and let
            // the wait loop reset it after wait_for_event unwinds.
            pending_disposed_session_ = std::move(owned);
        }
        else
        {
            // Tear the engine down on the dispatcher thread (it owns the COM objects).
            try
            {
                dispatcher_.invoke([&]() {
                    owned.reset();
                    return 0;
                });
            }
            catch (const std::exception &)
            {
                // Best effort: the engine may already be gone.
            }
        }
    }

    debugger_session_ = nullptr;
    detach_on_disconnect_ = false;
    is_execution_running_.store(false);
    reset_launch_configuration_state();
    clear_pending_stopped_event();
    session_stop_or_exit_observed_.store(true);
    conditional_breakpoint_conditions_by_id_.clear();
    conditional_breakpoint_ids_by_path_.clear();
    reset_suspended_state();
    suppress_session_events_.store(false);
    terminate_debuggee_on_disconnect_ = false;
    kernel_initial_resume_pending_.store(false);
    skip_next_break_hit_event_.store(false);
}

void dap_server::disconnect_debugger_session(const disconnect_request_options &options)
{
    if (debugger_session_ == nullptr)
    {
        return;
    }
    debugger::debugger_session &session = *debugger_session_;

    // Disarm the kernel initial-break swallow so a teardown break-in is never
    // auto-resumed (which would re-block the dispatcher).
    kernel_initial_resume_pending_.store(false);

    if (session.is_kernel())
    {
        disconnect_kernel_leaving_machine_running(session);
        return;
    }

    // If the target is running, the dispatcher thread is blocked inside
    // wait_for_event, so a dispatcher-bound detach/dispose would queue behind it
    // forever. Break in off-dispatcher first (thread-safe SetInterrupt).
    if (is_execution_running_.load())
    {
        try
        {
            session.interrupt();
        }
        catch (...)
        {
        }
        is_execution_running_.store(false);
    }

    const bool should_terminate_debuggee = options.terminate_debuggee.value_or(terminate_debuggee_on_disconnect_);
    const bool should_detach_debuggee =
        !should_terminate_debuggee && (detach_on_disconnect_ || terminate_debuggee_on_disconnect_);
    if (should_detach_debuggee)
    {
        dispatcher_.invoke([&]() {
            session.detach_current_process();
            return 0;
        });
    }

    dispose_debugger_session();
}

void dap_server::disconnect_kernel_leaving_machine_running(debugger::debugger_session &session)
{
    try
    {
        if (is_execution_running_.load())
        {
            try
            {
                session.interrupt();
            }
            catch (...)
            {
            }
        }

        dispatcher_.invoke([&]() {
            session.clear_all_breakpoints();
            session.continue_();
            return 0;
        });

        // Pump wait_for_event on a detached background thread so the resume packet
        // reaches the target; it blocks once the machine is running, which is fine
        // because the adapter exits right after disconnect.
        debugger::debugger_session *raw = &session;
        std::thread([this, raw]() {
            try
            {
                dispatcher_.invoke([raw]() {
                    raw->wait_for_event();
                    return 0;
                });
            }
            catch (...)
            {
            }
        }).detach();

        std::this_thread::sleep_for(std::chrono::milliseconds(kKernelResumeSettleMilliseconds));
    }
    catch (...)
    {
    }

    // Deliberately do NOT dispose the engine: releasing it while broken in is what
    // froze the machine. Release ownership without ending the session.
    debugger_session_.release();
    debugger_session_ = nullptr;
    is_execution_running_.store(false);
    detach_on_disconnect_ = false;
    terminate_debuggee_on_disconnect_ = false;
    session_stop_or_exit_observed_.store(true);
}

debugger::debugger_session &dap_server::require_debugger_session()
{
    if (debugger_session_ == nullptr)
    {
        throw std::runtime_error("No active debugger session is available.");
    }
    return *debugger_session_;
}

void dap_server::apply_session_configuration(debugger::debugger_session &session,
                                             const session_configuration &configuration)
{
    dispatcher_.invoke([&]() {
        session.enable_source_line_support();
        return 0;
    });
    if (configuration.symbol_path.has_value())
    {
        dispatcher_.invoke([&]() {
            session.set_symbol_path(*configuration.symbol_path);
            return 0;
        });
    }
    if (configuration.source_path.has_value())
    {
        dispatcher_.invoke([&]() {
            session.set_source_path(*configuration.source_path);
            return 0;
        });
    }
}

void dap_server::wire_debugger_session(debugger::debugger_session &session)
{
    debugger::debugger_session *self = &session;

    session.on_breakpoint_hit = [this, self](std::uint32_t breakpoint_id) {
        if (try_auto_continue_conditional_breakpoint_hit(*self, breakpoint_id))
        {
            return;
        }
        if (is_represented_stop_while_halted())
        {
            return;
        }
        is_execution_running_.store(false);
        session_stop_or_exit_observed_.store(true);
        if (suppress_session_events_.load())
        {
            clear_pending_stopped_event();
            return;
        }
        set_pending_stopped_event(protocol::StoppedEventBodyReason::Breakpoint,
                                  fmt::format("Paused on breakpoint {}.", breakpoint_id),
                                  std::vector<int>{static_cast<int>(breakpoint_id)});
    };

    session.on_exception_hit = [this, self](int exception_code) {
        if (try_auto_continue_kernel_initial_break(*self))
        {
            return;
        }
        if (is_represented_stop_while_halted())
        {
            return;
        }
        is_execution_running_.store(false);
        session_stop_or_exit_observed_.store(true);
        if (suppress_session_events_.load())
        {
            clear_pending_stopped_event();
            return;
        }
        // Don't overwrite a reason the request thread already set (pause/step). In
        // kernel mode the break-in surfaces as a 0x80000003 exception.
        if (!has_pending_stopped_event())
        {
            set_pending_stopped_event(
                protocol::StoppedEventBodyReason::Exception,
                fmt::format("Paused on exception 0x{:08X}.", static_cast<std::uint32_t>(exception_code)));
        }
    };

    session.on_break_hit = [this]() {
        if (skip_next_break_hit_event_.load())
        {
            skip_next_break_hit_event_.store(false);
            return;
        }
        is_execution_running_.store(false);
        session_stop_or_exit_observed_.store(true);

        // Read-and-clear under the lock so a pause set on the request thread is
        // visible here on the dispatcher thread.
        std::optional<pending_stopped_event> pending;
        {
            std::lock_guard<std::mutex> lock(pending_stopped_gate_);
            pending = pending_stopped_event_;
            pending_stopped_event_.reset();
        }

        if (suppress_session_events_.load() || !pending.has_value())
        {
            return;
        }

        flush_pending_thread_events();
        queue_session_event(nlohmann::json(factory::create_stopped_event(
            pending->reason, pending->description, try_get_current_thread_id(), pending->hit_breakpoint_ids)));
    };

    session.on_process_exited = [this](int exit_code) { queue_session_termination(exit_code); };
    session.on_session_ended = [this]() { queue_session_ended(); };
    session.on_thread_started = [this]() { queue_thread_event(protocol::ThreadEventBodyReason::Started); };
    session.on_thread_exited = [this]() { queue_thread_event(protocol::ThreadEventBodyReason::Exited); };
    session.on_output_received = [this](const std::string &output) {
        if (!output.empty())
        {
            queue_session_event(nlohmann::json(factory::create_output_event(output)));
        }
    };
}

// ---- Threads / current thread ----------------------------------------------

std::vector<protocol::Thread> dap_server::get_threads()
{
    return read_session_data_or_default<>(
        [this](debugger::debugger_session &session) {
            const std::optional<int> current = try_get_current_thread_id();
            const std::vector<std::uint32_t> ids = dispatcher_.invoke([&]() { return session.get_thread_ids(); });
            return factory::create_threads(ids, current);
        },
        std::vector<protocol::Thread>{});
}

bool dap_server::is_session_unavailable() const
{
    return should_exit_ || debugger_session_ == nullptr;
}

std::optional<int> dap_server::try_get_current_thread_id()
{
    // Capture the session pointer up front into the dispatcher call. The
    // dispatched lambda must not re-read the member,
    // which can be nulled concurrently when the session ends mid-flight — the
    // engine object itself stays alive (parked, drained off the callback) until
    // after this dispatched call has run.
    debugger::debugger_session *session = debugger_session_.get();
    if (session == nullptr)
    {
        return std::nullopt;
    }
    try
    {
        const std::uint32_t id = dispatcher_.invoke([session]() { return session->get_current_thread_id(); });
        if (id > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        {
            return std::nullopt;
        }
        return static_cast<int>(id);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

// ---- Pending-stopped gate ---------------------------------------------------

void dap_server::set_pending_stopped_event(protocol::StoppedEventBodyReason reason,
                                           std::optional<std::string> description,
                                           std::optional<std::vector<int>> hit_breakpoint_ids)
{
    std::lock_guard<std::mutex> lock(pending_stopped_gate_);
    pending_stopped_event_ = pending_stopped_event{reason, std::move(description), std::move(hit_breakpoint_ids)};
}

void dap_server::clear_pending_stopped_event()
{
    std::lock_guard<std::mutex> lock(pending_stopped_gate_);
    pending_stopped_event_.reset();
}

bool dap_server::has_pending_stopped_event()
{
    std::lock_guard<std::mutex> lock(pending_stopped_gate_);
    return pending_stopped_event_.has_value();
}

bool dap_server::is_represented_stop_while_halted() const
{
    return !is_execution_running_.load();
}

// ---- Stepping / suspended state --------------------------------------------

void dap_server::prepare_stepping_granularity(debugger::debugger_session &session,
                                              std::optional<protocol::SteppingGranularity> granularity)
{
    if (granularity == protocol::SteppingGranularity::Instruction)
    {
        dispatcher_.invoke([&]() {
            session.enable_instruction_stepping();
            return 0;
        });
        restore_source_line_stepping_after_stop_ = true;
        return;
    }

    dispatcher_.invoke([&]() {
        session.enable_source_line_stepping();
        return 0;
    });
    restore_source_line_stepping_after_stop_ = false;
}

void dap_server::restore_source_line_stepping_if_needed(debugger::debugger_session &session)
{
    if (!restore_source_line_stepping_after_stop_)
    {
        return;
    }
    try
    {
        dispatcher_.invoke([&]() {
            session.enable_source_line_stepping();
            return 0;
        });
    }
    catch (const std::exception &)
    {
        // Ignore when the session became unavailable.
    }
    restore_source_line_stepping_after_stop_ = false;
}

void dap_server::reset_launch_configuration_state()
{
    launch_awaiting_configuration_done_ = false;
    launch_stop_at_entry_ = false;
    launch_thread_id_.reset();
}

void dap_server::reset_suspended_state()
{
    frame_contexts_.clear();
    variables_by_reference_.clear();
    next_variables_reference_ = 1;
    {
        std::lock_guard<std::mutex> lock(pending_thread_events_gate_);
        pending_thread_events_.clear();
    }
}

void dap_server::continue_after_configuration_done()
{
    if (debugger_session_ == nullptr || is_execution_running_.load())
    {
        return;
    }
    debugger::debugger_session &session = *debugger_session_;

    const std::optional<int> thread_id =
        launch_thread_id_.has_value() ? launch_thread_id_ : try_get_current_thread_id();
    if (!thread_id.has_value())
    {
        spdlog::error("Unable to determine the current thread id for post-configuration continue.");
        return;
    }

    try
    {
        reset_suspended_state();
        is_execution_running_.store(true);
        dispatcher_.invoke([&]() {
            session.continue_();
            return 0;
        });
        send(nlohmann::json(factory::create_continued_event(*thread_id, true)));
        start_waiting_for_session_event(session);
    }
    catch (const std::exception &exception)
    {
        is_execution_running_.store(false);
        spdlog::error("Failed to continue after configurationDone: {}", exception.what());
    }
}

// ---- Frame / variable context maps -----------------------------------------

void dap_server::register_frames(int thread_id, const std::vector<debugger::stack_frame_info> &frames)
{
    for (const auto &frame : frames)
    {
        const int frame_id = factory::create_frame_id(thread_id, frame.frame_number);
        frame_contexts_[frame_id] = frame_context{thread_id, frame.frame_number, frame.source};
    }
}

int dap_server::create_variables_reference(const frame_context &context, variable_container_kind kind,
                                           const std::vector<protocol::Variable> &variables)
{
    if (variables.empty())
    {
        return 0;
    }
    const int reference = next_variables_reference_++;
    variables_by_reference_[reference] =
        variable_container_context{context.thread_id, context.frame_number, kind, variables};
    return reference;
}

protocol::Variable dap_server::create_variable(const debugger::named_value_info &value)
{
    protocol::Variable variable;
    variable.name = value.name;
    variable.value = value.value;
    variable.evaluate_name = value.name;
    variable.variables_reference = 0;
    return variable;
}

protocol::Variable dap_server::build_variable_tree(const debugger::variable_node &node, const frame_context &context,
                                                   const std::string &parent_evaluate_name)
{
    protocol::Variable variable;
    variable.name = node.name;
    variable.value = node.value;
    if (!node.type.empty())
    {
        variable.type = node.type;
    }

    // Full access path: a field appends ".field" to its parent; an array element
    // ("[0]") appends directly. A child with no name (anonymous union / base
    // subobject) cannot form a valid expression, so it inherits the parent's path.
    std::string evaluate_name = parent_evaluate_name;
    if (!node.name.empty())
    {
        if (parent_evaluate_name.empty() || node.name.front() == '[')
        {
            evaluate_name = parent_evaluate_name + node.name;
        }
        else
        {
            evaluate_name = parent_evaluate_name + "." + node.name;
        }
    }
    variable.evaluate_name = evaluate_name;

    if (!node.children.empty())
    {
        std::vector<protocol::Variable> children;
        children.reserve(node.children.size());
        for (const auto &child : node.children)
        {
            children.push_back(build_variable_tree(child, context, evaluate_name));
        }
        variable.named_variables = static_cast<int>(children.size());
        variable.variables_reference =
            create_variables_reference(context, variable_container_kind::structure, children);
    }
    else
    {
        variable.variables_reference = 0;
    }
    return variable;
}

protocol::Scope dap_server::create_scope(const std::string &name, protocol::ScopePresentationHint presentation_hint,
                                         int variables_reference, int named_variables,
                                         const std::optional<debugger::source_location> &source)
{
    protocol::Scope scope;
    scope.name = name;
    scope.presentation_hint = presentation_hint;
    scope.variables_reference = variables_reference;
    scope.named_variables = named_variables;
    scope.expensive = false;
    if (source.has_value())
    {
        protocol::Source dap_source;
        dap_source.name = std::filesystem::path(source->path).filename().string();
        dap_source.path = source->path;
        scope.source = dap_source;
        scope.line = source->line;
        scope.column = 1;
    }
    return scope;
}

void dap_server::register_locals_container_for_test(int variables_reference,
                                                    const std::vector<protocol::Variable> &variables)
{
    variables_by_reference_[variables_reference] =
        variable_container_context{1, 0u, variable_container_kind::locals, variables};
}

// ---- Conditional / kernel auto-continue ------------------------------------

void dap_server::update_conditional_breakpoint_state(const std::string &source_path,
                                                     const std::vector<debugger::source_breakpoint_result> &configured,
                                                     const std::vector<protocol::SourceBreakpoint> &requested)
{
    clear_conditional_breakpoint_state(source_path);

    const std::size_t count = std::min(configured.size(), requested.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const protocol::SourceBreakpoint &request = requested[index];
        const debugger::source_breakpoint_result &result = configured[index];
        if (!request.condition.has_value() || util::is_blank(*request.condition) || !result.verified ||
            !result.id.has_value())
        {
            continue;
        }
        const int breakpoint_id = *result.id;
        conditional_breakpoint_conditions_by_id_[breakpoint_id] = *request.condition;
        conditional_breakpoint_ids_by_path_[source_path].push_back(breakpoint_id);
    }
}

void dap_server::clear_conditional_breakpoint_state(const std::string &source_path)
{
    const auto it = conditional_breakpoint_ids_by_path_.find(source_path);
    if (it == conditional_breakpoint_ids_by_path_.end())
    {
        return;
    }
    for (const int breakpoint_id : it->second)
    {
        conditional_breakpoint_conditions_by_id_.erase(breakpoint_id);
    }
    conditional_breakpoint_ids_by_path_.erase(it);
}

std::vector<debugger::source_breakpoint_result> dap_server::set_breakpoints_while_running(
    debugger::debugger_session &session, const std::string &source_path,
    const std::vector<debugger::source_breakpoint_spec> &specs)
{
    // The target is running, so the dispatcher is blocked in the wait loop's
    // wait_for_event. Interrupt to a clean stop with events suppressed, let the
    // wait loop unwind (freeing the dispatcher), then apply the breakpoints exactly
    // as in the stopped state and resume. The user never sees the transient stop.
    std::vector<debugger::source_breakpoint_result> configured;
    run_with_suppressed_session_events([&]() {
        session.interrupt();
        join_wait_loop(); // returns once the suppressed break unwinds the wait loop

        configured = dispatcher_.invoke([&]() { return session.set_source_breakpoints(source_path, specs); });

        is_execution_running_.store(true);
        dispatcher_.invoke([&]() {
            session.continue_();
            return 0;
        });
    });
    start_waiting_for_session_event(session);
    return configured;
}

bool dap_server::try_auto_continue_conditional_breakpoint_hit(debugger::debugger_session &session,
                                                              std::uint32_t breakpoint_id)
{
    const auto it = conditional_breakpoint_conditions_by_id_.find(static_cast<int>(breakpoint_id));
    if (it == conditional_breakpoint_conditions_by_id_.end())
    {
        return false;
    }

    const std::optional<bool> result = try_evaluate_conditional_breakpoint(session, it->second);
    if (result.value_or(true)) // not false -> surface the stop
    {
        return false;
    }

    reset_suspended_state();
    clear_pending_stopped_event();
    session_stop_or_exit_observed_.store(false);
    is_execution_running_.store(true);
    skip_next_break_hit_event_.store(true);
    session.continue_();
    return true;
}

bool dap_server::try_auto_continue_kernel_initial_break(debugger::debugger_session &session)
{
    if (!kernel_initial_resume_pending_.load() || suppress_session_events_.load())
    {
        return false;
    }

    kernel_initial_resume_pending_.store(false);
    reset_suspended_state();
    clear_pending_stopped_event();
    session_stop_or_exit_observed_.store(false);
    is_execution_running_.store(true);
    skip_next_break_hit_event_.store(true);
    session.continue_();
    return true;
}

std::optional<bool> dap_server::try_evaluate_conditional_breakpoint(debugger::debugger_session &session,
                                                                    const std::string &condition)
{
    try
    {
        const std::string output =
            session.execute_command_with_output(fmt::format("dx ({})", condition), /*suppress_output_events=*/true);
        static const std::regex pattern(R"(\b(?:true|false)\b)", std::regex::icase);
        std::string last;
        for (auto it = std::sregex_iterator(output.begin(), output.end(), pattern); it != std::sregex_iterator(); ++it)
        {
            last = it->str();
        }
        if (last.empty())
        {
            return std::nullopt;
        }
        std::string lowered = last;
        for (char &c : lowered)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return lowered == "true";
    }
    catch (...)
    {
        return std::nullopt;
    }
}

// ---- Pure stackTrace-window helpers (test seams) ---------------------------

dap_server::stack_trace_window dap_server::read_stack_trace_window(const protocol::StackTraceArguments &arguments)
{
    const auto clamp = [](const std::optional<std::int64_t> &value) -> int {
        if (value.has_value() && *value > 0 && *value <= std::numeric_limits<int>::max())
        {
            return static_cast<int>(*value);
        }
        return 0;
    };
    return stack_trace_window{clamp(arguments.start_frame), clamp(arguments.levels)};
}

std::vector<debugger::stack_frame_info> dap_server::apply_stack_trace_window(
    const std::vector<debugger::stack_frame_info> &frames, const stack_trace_window &window)
{
    std::vector<debugger::stack_frame_info> result;
    std::size_t start = window.start_frame > 0 ? static_cast<std::size_t>(window.start_frame) : 0;
    for (std::size_t index = start; index < frames.size(); ++index)
    {
        if (window.levels > 0 && result.size() >= static_cast<std::size_t>(window.levels))
        {
            break;
        }
        result.push_back(frames[index]);
    }
    return result;
}

dap_server::stack_trace_result dap_server::create_stack_trace_result(
    const std::vector<debugger::stack_frame_info> &all_frames, int total_frames, const stack_trace_window &window)
{
    return stack_trace_result{apply_stack_trace_window(all_frames, window), total_frames};
}

int dap_server::get_stack_trace_fetch_limit(const stack_trace_window &window)
{
    if (window.levels <= 0)
    {
        return std::numeric_limits<int>::max();
    }
    const long long requested = static_cast<long long>(window.start_frame) + window.levels;
    return requested >= std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : static_cast<int>(requested);
}

std::string dap_server::create_evaluate_command(const std::string &expression,
                                                std::optional<std::uint32_t> frame_number)
{
    if (expression.empty() || util::is_blank(expression))
    {
        throw std::invalid_argument("expression must be non-empty");
    }
    return frame_number.has_value() ? fmt::format(".frame {};{}", *frame_number, expression) : expression;
}
} // namespace dap_dbgeng::service
