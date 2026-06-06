#include "debugger/debugger_session.h"

#include "debugger/debugger_session_internal.h"

namespace dap_dbgeng::debugger
{
// ---------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------
std::vector<std::uint32_t> debugger_session::get_thread_ids()
{
    throw_if_disposed();

    ULONG thread_count = 0;
    check_hr(system_objects_->GetNumberThreads(&thread_count), "Could not enumerate threads");
    std::vector<ULONG> system_ids(thread_count);
    if (thread_count > 0)
    {
        check_hr(system_objects_->GetThreadIdsByIndex(0, thread_count, nullptr, system_ids.data()),
                 "Could not read thread ids");
    }
    return std::vector<std::uint32_t>(system_ids.begin(), system_ids.end());
}

std::uint32_t debugger_session::get_current_thread_id()
{
    throw_if_disposed();

    ULONG engine_thread_id = 0;
    check_hr(system_objects_->GetCurrentThreadId(&engine_thread_id), "Could not read current thread id");

    ULONG thread_count = 0;
    check_hr(system_objects_->GetNumberThreads(&thread_count), "Could not enumerate threads");
    std::vector<ULONG> system_ids(thread_count);
    if (thread_count > 0)
    {
        check_hr(system_objects_->GetThreadIdsByIndex(0, thread_count, nullptr, system_ids.data()),
                 "Could not read thread ids");
    }

    if (engine_thread_id >= system_ids.size())
    {
        throw std::runtime_error(fmt::format("Current engine thread id {} is out of range.", engine_thread_id));
    }
    return system_ids[engine_thread_id];
}

void debugger_session::set_current_thread(std::uint32_t system_thread_id)
{
    throw_if_disposed();
    if (is_kernel_)
    {
        set_current_thread_kernel(system_thread_id);
    }
    else
    {
        set_current_thread_user(system_thread_id);
    }
}

void debugger_session::set_current_thread_user(std::uint32_t system_thread_id)
{
    ULONG engine_thread_id = 0;
    check_hr(system_objects_->GetThreadIdBySystemId(system_thread_id, &engine_thread_id),
             "Could not resolve engine thread id");
    check_hr(system_objects_->SetCurrentThreadId(engine_thread_id), "Could not select thread");
}

void debugger_session::set_current_thread_kernel(std::uint32_t system_thread_id)
{
    ULONG thread_count = 0;
    check_hr(system_objects_->GetNumberThreads(&thread_count), "Could not enumerate threads");
    std::vector<ULONG> system_ids(thread_count);
    if (thread_count > 0)
    {
        check_hr(system_objects_->GetThreadIdsByIndex(0, thread_count, nullptr, system_ids.data()),
                 "Could not read thread ids");
    }

    int index = -1;
    for (std::size_t i = 0; i < system_ids.size(); ++i)
    {
        if (system_ids[i] == system_thread_id)
        {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0)
    {
        throw std::runtime_error(
            fmt::format("Thread id {} is not a current kernel processor context.", system_thread_id));
    }
    check_hr(system_objects_->SetCurrentThreadId(static_cast<ULONG>(index)), "Could not select thread");
}
} // namespace dap_dbgeng::debugger
