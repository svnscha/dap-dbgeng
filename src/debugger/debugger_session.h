#pragma once

#include "debugger/debugger_types.h"

namespace dap_dbgeng::debugger
{
// Direct dbgeng.h COM wrapper around a single debug-engine session, calling
// IDebugClient5 / IDebugControl7 / IDebugSymbols3 / IDebugSystemObjects /
// IDebugRegisters2 / IDebugDataSpaces4 / IDebugSymbolGroup2 / IDebugBreakpoint
// directly.
//
// Threading: dbgeng has thread affinity. Every method (except interrupt(),
// which uses the cross-thread-safe IDebugControl::SetInterrupt) must be called
// from the single dbgeng worker thread that constructed the session.
class debugger_session
{
  public:
    // Constructs the engine. enginePath may be a directory or a path to
    // dbgeng.dll; the containing directory's dbgeng.dll is LoadLibrary'd first so
    // the engine and its peers resolve, then DebugCreate(IDebugClient) is called.
    explicit debugger_session(const std::string &engine_path);
    ~debugger_session();

    debugger_session(const debugger_session &) = delete;
    debugger_session &operator=(const debugger_session &) = delete;

    // Event callbacks. Raised from the dbgeng callbacks.
    std::function<void(std::uint32_t)> on_breakpoint_hit;
    std::function<void(int)> on_exception_hit;
    std::function<void()> on_break_hit;
    std::function<void(int)> on_process_exited;
    std::function<void()> on_session_ended;
    std::function<void()> on_thread_started;
    std::function<void()> on_thread_exited;
    std::function<void(const std::string &)> on_output_received;

    // Process server (dbgsrv) -------------------------------------------------
    void connect_process_server(const std::string &connection_string);
    void disconnect_process_server();

    // Enumerate running processes via the engine. With no process server connected
    // this lists local processes; after connect_process_server it lists processes
    // on the remote (dbgsrv) host. Used by the adapter's --list-processes mode.
    std::vector<process_info> list_processes();
    bool is_remote() const
    {
        return process_server_handle_ != 0;
    }
    std::uint64_t process_server_handle() const;

    // Targets -----------------------------------------------------------------
    void launch(const std::string &executable_path, std::optional<std::string> arguments = std::nullopt,
                std::optional<std::string> working_directory = std::nullopt);
    void attach(int process_id);
    void attach_remote(const std::string &connection_string, int process_id);
    void attach_kernel(const std::string &connection_string);
    void open_dump_file(const std::string &dump_file_path);

    // Execution ---------------------------------------------------------------
    void continue_();
    void step_into();
    void step_over();
    void step_out();
    void enable_instruction_stepping();
    void interrupt();
    void wait_for_event(std::uint32_t timeout = INFINITE);

    // Breakpoints -------------------------------------------------------------
    std::uint32_t set_breakpoint(const std::string &file_path, int line);
    std::vector<source_breakpoint_result> set_source_breakpoints(const std::string &file_path,
                                                                 const std::vector<int> &lines);
    std::vector<source_breakpoint_result> set_source_breakpoints(
        const std::string &file_path, const std::vector<source_breakpoint_spec> &breakpoints);
    // Function-name breakpoints ("bu name"). Each call replaces the previous
    // set, mirroring DAP setFunctionBreakpoints semantics. Results are in
    // request order; verified means the engine accepted the location.
    std::vector<source_breakpoint_result> set_function_breakpoints(const std::vector<std::string> &names);
    // Instruction (address) breakpoints ("bp <address>"). Replaces the previous set.
    std::vector<source_breakpoint_result> set_instruction_breakpoints(const std::vector<std::uint64_t> &addresses);
    // Data (hardware) breakpoints ("ba [r|w]<size> <address>"). Replaces the previous set.
    std::vector<source_breakpoint_result> set_data_breakpoints(const std::vector<data_breakpoint_spec> &breakpoints);
    // Break on first-chance C++ exceptions (sxe/sxd e06d7363).
    void set_cpp_first_chance_break(bool enabled);
    std::vector<breakpoint_state> get_breakpoint_states();
    void clear_all_breakpoints();

    // Stack / vars ------------------------------------------------------------
    std::vector<stack_frame_info> get_stack_trace(int max_frames = 128);
    stack_trace_details get_stack_trace_details(int max_frames = 128);
    std::vector<named_value_info> get_locals(std::uint32_t frame_number);
    // Locals for a frame as a tree, with struct/class members expanded (up to a
    // depth and node-count cap). Used by the scopes/variables handlers to render
    // expandable locals; get_locals stays the flat form used by setVariable.
    std::vector<variable_node> get_locals_tree(std::uint32_t frame_number);
    // Assigns a local or a nested struct field, named by its in-scope expression
    // (e.g. "t.origin.x"), through the engine's symbol group - the native write
    // counterpart of get_locals_tree's reads. Returns the field's refreshed
    // value/type. Throws if the expression cannot be resolved or assigned.
    variable_node set_local_value(std::uint32_t frame_number, const std::string &expression, const std::string &value);
    // Reads a frame-scoped expression (e.g. "t.origin.x") through the scope
    // symbol group - the read counterpart of set_local_value.
    variable_node get_local_value(std::uint32_t frame_number, const std::string &expression);
    // Resolves an in-scope expression (e.g. "watched" or "t.origin.x") to its
    // absolute address and byte size, for data breakpoints and memory views.
    // Throws if the expression cannot be resolved or has no address.
    std::pair<std::uint64_t, std::uint32_t> get_symbol_address(std::uint32_t frame_number,
                                                               const std::string &expression);
    std::vector<named_value_info> get_registers(std::uint32_t frame_number);
    std::optional<source_location> try_get_frame_source(std::uint64_t instruction_offset);

    // Threads -----------------------------------------------------------------
    std::vector<std::uint32_t> get_thread_ids();
    std::uint32_t get_current_thread_id();
    void set_current_thread(std::uint32_t system_thread_id);

    // Symbols / source --------------------------------------------------------
    void set_symbol_path(const std::vector<std::string> &symbol_paths);
    void set_source_path(const std::vector<std::string> &source_paths);
    void enable_source_line_support();
    void enable_source_line_stepping();
    void reload_symbols(std::optional<std::string> module_name = std::nullopt);

    // Memory ------------------------------------------------------------------
    std::vector<disassembled_instruction_info> disassemble(std::uint64_t memory_address, int instruction_offset,
                                                           int instruction_count, bool resolve_symbols);
    // Reads up to `count` bytes of debuggee virtual memory; the result may be
    // shorter when the range is partially unreadable.
    std::vector<unsigned char> read_memory(std::uint64_t address, std::uint32_t count);
    // Writes bytes to debuggee virtual memory; returns the bytes written.
    std::uint32_t write_memory(std::uint64_t address, const std::vector<unsigned char> &bytes);

    // Modules -------------------------------------------------------------------
    std::vector<module_info> get_modules();

    // The last exception event observed, if any (valid while stopped on it).
    std::optional<last_exception_info> get_last_exception() const
    {
        return last_exception_;
    }

    // Command -----------------------------------------------------------------
    std::string execute_command_with_output(const std::string &command, bool suppress_output_events = false);

    // Detach / terminate ------------------------------------------------------
    void detach_current_process();
    void detach_all_processes();
    void terminate_current_process();

    // Props -------------------------------------------------------------------
    bool is_kernel() const
    {
        return is_kernel_;
    }

    // Test helpers. ----------------------------------------------------------
    static bool try_parse_breakpoint_id(const std::string &description, std::uint32_t &breakpoint_id);
    static int get_stack_trace_fetch_count(int total_frames, int max_frames);

  private:
    // COM event-callback shim, raises the std::function members.
    class event_callbacks;
    // COM output-callback shim, routes text into the capture buffer / forwards.
    class output_callbacks;

    void wire_events();
    void execute_debugger_command(const std::string &command, const std::string &message);
    void throw_if_disposed() const;

    void attach_process_native(std::uint64_t process_server_handle, int process_id);
    void disconnect_process_server_core();

    void set_current_thread_user(std::uint32_t system_thread_id);
    void set_current_thread_kernel(std::uint32_t system_thread_id);

    // Disassemble `count` instructions forward from / backward up to an address using
    // the engine's instruction decoder (IDebugControl::Disassemble), reading the raw
    // bytes via the data spaces interface.
    std::vector<disassembled_instruction_info> disassemble_range(std::uint64_t start_address, int count,
                                                                 bool resolve_symbols);
    std::vector<disassembled_instruction_info> disassemble_backward(std::uint64_t end_address, int count,
                                                                    bool resolve_symbols);
    std::optional<disassembled_instruction_info> disassemble_one(std::uint64_t address, bool resolve_symbols,
                                                                 std::uint64_t &end_offset);
    std::optional<std::string> try_get_frame_name(std::uint64_t instruction_offset);
    bool try_get_symbol_start(std::uint64_t instruction_offset, std::uint64_t &symbol_start);

    std::uint32_t get_last_breakpoint_id();
    std::vector<int> get_configured_breakpoint_ids();
    void clear_source_breakpoints(const std::string &normalized_path);
    // Replace-all helper shared by the function/instruction/data breakpoint
    // setters: clears `tracked_ids`, runs one engine command per breakpoint
    // (id-diffing to learn the new id), and re-fills `tracked_ids`.
    std::vector<source_breakpoint_result> set_command_breakpoints(std::vector<int> &tracked_ids,
                                                                  const std::vector<std::string> &commands,
                                                                  const std::vector<std::string> &failure_labels);

    // Scopes to `frame_number`, opens the scope symbol group, and adds
    // `expression` as a symbol; returns the owned group (caller releases) and
    // fills `symbol_index`. Shared by the local-value and symbol-address paths.
    IDebugSymbolGroup2 *open_scope_symbol_group(std::uint32_t frame_number, const std::string &expression,
                                                ULONG &symbol_index);

    // Output capture.
    void begin_output_capture(bool suppress_output_events);
    std::string end_output_capture();
    void discard_output_capture();
    void handle_output_received(const std::string &text);

    // The engine handle (QI'd into every IDebugXxx interface we need).
    HMODULE dbgeng_module_ = nullptr;
    IDebugClient5 *client_ = nullptr;
    IDebugControl7 *control_ = nullptr;
    IDebugSymbols3 *symbols_ = nullptr;
    IDebugSystemObjects *system_objects_ = nullptr;
    IDebugRegisters2 *registers_ = nullptr;
    IDebugDataSpaces4 *data_spaces_ = nullptr;

    event_callbacks *event_callbacks_ = nullptr;
    output_callbacks *output_callbacks_ = nullptr;

    bool disposed_ = false;
    bool terminate_debuggee_on_dispose_ = true;
    bool is_kernel_ = false;
    std::uint64_t process_server_handle_ = 0;

    std::mutex output_capture_sync_;
    std::unique_ptr<std::string> output_capture_;
    bool suppress_captured_output_forwarding_ = false;

    // Per-path configured breakpoint ids (case-insensitive key), so a new
    // setSourceBreakpoints for a file clears the file's previous breakpoints.
    std::map<std::string, std::vector<int>> source_breakpoint_ids_by_path_;

    // Per-kind configured breakpoint ids, so each replace-all DAP request
    // (setFunctionBreakpoints / setInstructionBreakpoints / setDataBreakpoints)
    // clears only its own previous set.
    std::vector<int> function_breakpoint_ids_;
    std::vector<int> instruction_breakpoint_ids_;
    std::vector<int> data_breakpoint_ids_;

    // Last exception event, captured by the engine callback; cleared whenever
    // execution resumes so exceptionInfo never describes a stale stop.
    std::optional<last_exception_info> last_exception_;
    // Mirrors the sxe/sxd e06d7363 filter so the exception callback lets
    // first-chance C++ exceptions run when the filter is off.
    bool cpp_first_chance_break_ = false;
};
} // namespace dap_dbgeng::debugger
