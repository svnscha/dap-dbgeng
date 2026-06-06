#include "debugger/debugger_session.h"

#include "debugger/debugger_session_internal.h"

namespace dap_dbgeng::debugger
{
// ---------------------------------------------------------------------------
// Detach / terminate
// ---------------------------------------------------------------------------
void debugger_session::detach_current_process()
{
    throw_if_disposed();
    detach_all_processes();
}

void debugger_session::detach_all_processes()
{
    throw_if_disposed();
    terminate_debuggee_on_dispose_ = false;
    try
    {
        check_hr(client_->DetachProcesses(), "Could not detach processes");
    }
    catch (...)
    {
        terminate_debuggee_on_dispose_ = true;
        throw;
    }
}

void debugger_session::terminate_current_process()
{
    throw_if_disposed();
    client_->EndSession(DEBUG_END_ACTIVE_TERMINATE);
    terminate_debuggee_on_dispose_ = false;
}

// ---------------------------------------------------------------------------
// Command execution + output capture
// ---------------------------------------------------------------------------
std::string debugger_session::execute_command_with_output(const std::string &command, bool suppress_output_events)
{
    throw_if_disposed();
    throw_if_null_or_whitespace(command, "command");

    begin_output_capture(suppress_output_events);
    try
    {
        execute_debugger_command(command, fmt::format("Could not execute debugger command '{}'", command));
        return end_output_capture();
    }
    catch (...)
    {
        discard_output_capture();
        throw;
    }
}

void debugger_session::execute_debugger_command(const std::string &command, const std::string &message)
{
    throw_if_disposed();
    check_hr(control_->Execute(DEBUG_OUTCTL_THIS_CLIENT, command.c_str(), DEBUG_EXECUTE_DEFAULT), message);
}

void debugger_session::begin_output_capture(bool suppress_output_events)
{
    std::lock_guard<std::mutex> lock(output_capture_sync_);
    if (output_capture_ != nullptr)
    {
        throw std::runtime_error("A debugger output capture is already in progress.");
    }
    output_capture_ = std::make_unique<std::string>();
    suppress_captured_output_forwarding_ = suppress_output_events;
}

std::string debugger_session::end_output_capture()
{
    std::lock_guard<std::mutex> lock(output_capture_sync_);
    std::string output = output_capture_ ? trim_end(*output_capture_) : std::string();
    output_capture_.reset();
    suppress_captured_output_forwarding_ = false;
    return output;
}

void debugger_session::discard_output_capture()
{
    std::lock_guard<std::mutex> lock(output_capture_sync_);
    output_capture_.reset();
    suppress_captured_output_forwarding_ = false;
}

void debugger_session::handle_output_received(const std::string &text)
{
    bool should_forward = true;
    {
        std::lock_guard<std::mutex> lock(output_capture_sync_);
        if (output_capture_ != nullptr)
        {
            output_capture_->append(text);
        }
        should_forward = output_capture_ == nullptr || !suppress_captured_output_forwarding_;
    }

    if (should_forward && on_output_received)
    {
        on_output_received(text);
    }
}
} // namespace dap_dbgeng::debugger
