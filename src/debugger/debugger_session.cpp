#include "debugger/debugger_session.h"

#include "debugger/debugger_session_internal.h"

namespace dap_dbgeng::debugger
{

// ---------------------------------------------------------------------------
// COM event-callback sink.
// ---------------------------------------------------------------------------
class debugger_session::event_callbacks : public IDebugEventCallbacks
{
  public:
    explicit event_callbacks(debugger_session *owner) : owner_(owner)
    {
    }

    // IUnknown -------------------------------------------------------------
    STDMETHOD(QueryInterface)(REFIID iid, void **out) override
    {
        if (out == nullptr)
        {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDebugEventCallbacks))
        {
            *out = static_cast<IDebugEventCallbacks *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override
    {
        return ++ref_count_;
    }
    STDMETHOD_(ULONG, Release)() override
    {
        const ULONG count = --ref_count_;
        if (count == 0)
        {
            delete this;
        }
        return count;
    }

    // IDebugEventCallbacks -------------------------------------------------
    STDMETHOD(GetInterestMask)(PULONG mask) override
    {
        *mask = DEBUG_EVENT_BREAKPOINT | DEBUG_EVENT_CHANGE_ENGINE_STATE | DEBUG_EVENT_CREATE_PROCESS |
                DEBUG_EVENT_CREATE_THREAD | DEBUG_EVENT_EXCEPTION | DEBUG_EVENT_EXIT_PROCESS | DEBUG_EVENT_EXIT_THREAD |
                DEBUG_EVENT_SESSION_STATUS | DEBUG_EVENT_SYSTEM_ERROR;
        return S_OK;
    }

    STDMETHOD(Breakpoint)(PDEBUG_BREAKPOINT bp) override
    {
        (void)bp;
        if (owner_->on_breakpoint_hit)
        {
            owner_->on_breakpoint_hit(owner_->get_last_breakpoint_id());
        }
        return DEBUG_STATUS_BREAK;
    }

    STDMETHOD(Exception)(PEXCEPTION_RECORD64 exception, ULONG first_chance) override
    {
        if (exception != nullptr)
        {
            // Remember the event so an exceptionInfo request can describe the
            // stop. Set on the dispatcher thread (inside WaitForEvent), read
            // there too via dispatcher-marshaled calls.
            owner_->last_exception_ =
                last_exception_info{exception->ExceptionCode, exception->ExceptionAddress, first_chance != 0};
            if (owner_->on_exception_hit)
            {
                owner_->on_exception_hit(static_cast<int>(exception->ExceptionCode));
            }
        }
        return DEBUG_STATUS_BREAK;
    }

    STDMETHOD(CreateThread)(ULONG64 handle, ULONG64 data_offset, ULONG64 start_offset) override
    {
        (void)handle;
        (void)data_offset;
        (void)start_offset;
        if (owner_->on_thread_started)
        {
            owner_->on_thread_started();
        }
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(ExitThread)(ULONG exit_code) override
    {
        (void)exit_code;
        if (owner_->on_thread_exited)
        {
            owner_->on_thread_exited();
        }
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(CreateProcess)
    (ULONG64 image_file_handle, ULONG64 handle, ULONG64 base_offset, ULONG module_size, PCSTR module_name,
     PCSTR image_name, ULONG check_sum, ULONG time_date_stamp, ULONG64 initial_thread_handle,
     ULONG64 thread_data_offset, ULONG64 start_offset) override
    {
        (void)image_file_handle;
        (void)handle;
        (void)base_offset;
        (void)module_size;
        (void)module_name;
        (void)image_name;
        (void)check_sum;
        (void)time_date_stamp;
        (void)initial_thread_handle;
        (void)thread_data_offset;
        (void)start_offset;
        return DEBUG_STATUS_BREAK;
    }

    STDMETHOD(ExitProcess)(ULONG exit_code) override
    {
        if (owner_->on_process_exited)
        {
            owner_->on_process_exited(static_cast<int>(exit_code));
        }
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(LoadModule)
    (ULONG64 image_file_handle, ULONG64 base_offset, ULONG module_size, PCSTR module_name, PCSTR image_name,
     ULONG check_sum, ULONG time_date_stamp) override
    {
        (void)image_file_handle;
        (void)base_offset;
        (void)module_size;
        (void)module_name;
        (void)image_name;
        (void)check_sum;
        (void)time_date_stamp;
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(UnloadModule)(PCSTR image_base_name, ULONG64 base_offset) override
    {
        (void)image_base_name;
        (void)base_offset;
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(SystemError)(ULONG error, ULONG level) override
    {
        (void)level;
        if (owner_->on_exception_hit)
        {
            owner_->on_exception_hit(static_cast<int>(error));
        }
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(SessionStatus)(ULONG status) override
    {
        if (status == DEBUG_SESSION_END_SESSION_ACTIVE_TERMINATE || status == DEBUG_SESSION_END_SESSION_ACTIVE_DETACH ||
            status == DEBUG_SESSION_END_SESSION_PASSIVE)
        {
            if (owner_->on_session_ended)
            {
                owner_->on_session_ended();
            }
        }
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(ChangeDebuggeeState)(ULONG flags, ULONG64 argument) override
    {
        (void)flags;
        (void)argument;
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(ChangeEngineState)(ULONG flags, ULONG64 argument) override
    {
        if (flags == DEBUG_CES_EXECUTION_STATUS && argument == kStatusBreak)
        {
            if (owner_->on_break_hit)
            {
                owner_->on_break_hit();
            }
        }
        else if (flags == DEBUG_CES_EXECUTION_STATUS && argument == kStatusNoDebuggee)
        {
            if (owner_->on_session_ended)
            {
                owner_->on_session_ended();
            }
        }
        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD(ChangeSymbolState)(ULONG flags, ULONG64 argument) override
    {
        (void)flags;
        (void)argument;
        return DEBUG_STATUS_NO_CHANGE;
    }

  private:
    debugger_session *owner_;
    std::atomic<ULONG> ref_count_{1};
};

// ---------------------------------------------------------------------------
// COM output-callback sink.
// ---------------------------------------------------------------------------
class debugger_session::output_callbacks : public IDebugOutputCallbacks
{
  public:
    explicit output_callbacks(debugger_session *owner) : owner_(owner)
    {
    }

    STDMETHOD(QueryInterface)(REFIID iid, void **out) override
    {
        if (out == nullptr)
        {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDebugOutputCallbacks))
        {
            *out = static_cast<IDebugOutputCallbacks *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override
    {
        return ++ref_count_;
    }
    STDMETHOD_(ULONG, Release)() override
    {
        const ULONG count = --ref_count_;
        if (count == 0)
        {
            delete this;
        }
        return count;
    }

    STDMETHOD(Output)(ULONG mask, PCSTR text) override
    {
        (void)mask;
        if (text != nullptr && text[0] != '\0')
        {
            owner_->handle_output_received(text);
        }
        return S_OK;
    }

  private:
    debugger_session *owner_;
    std::atomic<ULONG> ref_count_{1};
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
debugger_session::debugger_session(const std::string &engine_path)
{
    throw_if_null_or_whitespace(engine_path, "enginePath");
    const std::string dbgeng_directory = get_dbgeng_directory(engine_path);

    // Load the engine's dbgeng.dll from the resolved directory so the engine and
    // its peers (dbghelp, symsrv, ...) resolve from the same location.
    const std::filesystem::path dll = std::filesystem::path(dbgeng_directory) / "dbgeng.dll";
    dbgeng_module_ = ::LoadLibraryW(dll.wstring().c_str());
    if (dbgeng_module_ == nullptr)
    {
        // Fall back to the system loader (in case the path was a bare directory
        // without dbgeng.dll but the engine is otherwise discoverable on PATH).
        dbgeng_module_ = ::LoadLibraryW(L"dbgeng.dll");
    }
    if (dbgeng_module_ == nullptr)
    {
        throw std::runtime_error(fmt::format("Could not load dbgeng.dll from '{}'.", dbgeng_directory));
    }

    using debug_create_t = HRESULT(WINAPI *)(REFIID, PVOID *);
    auto debug_create =
        reinterpret_cast<debug_create_t>(reinterpret_cast<void *>(::GetProcAddress(dbgeng_module_, "DebugCreate")));
    if (debug_create == nullptr)
    {
        throw std::runtime_error("Could not resolve DebugCreate in dbgeng.dll.");
    }

    IDebugClient *base_client = nullptr;
    check_hr(debug_create(__uuidof(IDebugClient), reinterpret_cast<PVOID *>(&base_client)),
             "Could not create the debug engine client");

    // QueryInterface the specific interface versions we drive.
    HRESULT hr = base_client->QueryInterface(__uuidof(IDebugClient5), reinterpret_cast<PVOID *>(&client_));
    if (SUCCEEDED(hr))
    {
        hr = base_client->QueryInterface(__uuidof(IDebugControl7), reinterpret_cast<PVOID *>(&control_));
    }
    if (SUCCEEDED(hr))
    {
        hr = base_client->QueryInterface(__uuidof(IDebugSymbols3), reinterpret_cast<PVOID *>(&symbols_));
    }
    if (SUCCEEDED(hr))
    {
        hr = base_client->QueryInterface(__uuidof(IDebugSystemObjects), reinterpret_cast<PVOID *>(&system_objects_));
    }
    if (SUCCEEDED(hr))
    {
        hr = base_client->QueryInterface(__uuidof(IDebugRegisters2), reinterpret_cast<PVOID *>(&registers_));
    }
    if (SUCCEEDED(hr))
    {
        hr = base_client->QueryInterface(__uuidof(IDebugDataSpaces4), reinterpret_cast<PVOID *>(&data_spaces_));
    }
    base_client->Release();
    check_hr(hr, "Could not query the debug engine interfaces");

    event_callbacks_ = new event_callbacks(this);   // ref count starts at 1
    output_callbacks_ = new output_callbacks(this); // ref count starts at 1

    wire_events();
}

debugger_session::~debugger_session()
{
    if (disposed_)
    {
        return;
    }

    if (client_ != nullptr)
    {
        client_->SetEventCallbacks(nullptr);
        client_->SetOutputCallbacks(nullptr);
    }

    if (terminate_debuggee_on_dispose_ && client_ != nullptr)
    {
        client_->EndSession(DEBUG_END_ACTIVE_TERMINATE);
    }

    try
    {
        disconnect_process_server_core();
    }
    catch (const std::exception &)
    {
    }

    if (event_callbacks_ != nullptr)
    {
        event_callbacks_->Release();
        event_callbacks_ = nullptr;
    }
    if (output_callbacks_ != nullptr)
    {
        output_callbacks_->Release();
        output_callbacks_ = nullptr;
    }

    if (data_spaces_ != nullptr)
    {
        data_spaces_->Release();
        data_spaces_ = nullptr;
    }
    if (registers_ != nullptr)
    {
        registers_->Release();
        registers_ = nullptr;
    }
    if (system_objects_ != nullptr)
    {
        system_objects_->Release();
        system_objects_ = nullptr;
    }
    if (symbols_ != nullptr)
    {
        symbols_->Release();
        symbols_ = nullptr;
    }
    if (control_ != nullptr)
    {
        control_->Release();
        control_ = nullptr;
    }
    if (client_ != nullptr)
    {
        client_->Release();
        client_ = nullptr;
    }

    if (dbgeng_module_ != nullptr)
    {
        ::FreeLibrary(dbgeng_module_);
        dbgeng_module_ = nullptr;
    }

    disposed_ = true;
}

void debugger_session::wire_events()
{
    check_hr(client_->SetEventCallbacks(event_callbacks_), "Could not register event callbacks");
    check_hr(client_->SetOutputCallbacks(output_callbacks_), "Could not register output callbacks");
}

void debugger_session::throw_if_disposed() const
{
    if (disposed_)
    {
        throw std::runtime_error("debugger_session has been disposed.");
    }
}
} // namespace dap_dbgeng::debugger
