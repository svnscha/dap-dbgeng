#include "util/debugger_session_dispatcher.h"

namespace dap_dbgeng::util
{
debugger_session_dispatcher::debugger_session_dispatcher()
{
    // Start the worker and publish its id before any invoke() can race the
    // short-circuit check (the promise blocks the ctor until the id is set).
    std::promise<std::thread::id> started;
    std::future<std::thread::id> ready = started.get_future();
    worker_ = std::thread([state = state_, &started] {
        started.set_value(std::this_thread::get_id());
        state->queue.run();
        state->finished.set_value();
    });
    worker_id_.store(ready.get());
}

debugger_session_dispatcher::~debugger_session_dispatcher()
{
    state_->queue.stop();
    if (!worker_.joinable())
    {
        return;
    }

    // The worker can be parked in an engine call that only the target can end -
    // disconnecting a kernel session deliberately leaves it inside
    // WaitForEvent, waiting for a machine that is running again and will not
    // report anything. Joining unconditionally hangs the adapter there: the
    // process never exits, its trace is never written, and it keeps the kernel
    // connection, so the next session cannot attach at all. Wait briefly for an
    // orderly finish, then leave the thread to the process teardown - it holds
    // its own reference to the state it is still using.
    if (state_->finished.get_future().wait_for(kWorkerShutdownGrace) == std::future_status::ready)
    {
        worker_.join();
    }
    else
    {
        worker_.detach();
    }
}

std::string debugger_session_dispatcher::unwrap_failure_message(const std::exception &exception,
                                                                const std::string &fallback)
{
    if (const auto *failure = dynamic_cast<const dispatcher_failure *>(&exception))
    {
        return failure->inner_message();
    }
    return fallback;
}
} // namespace dap_dbgeng::util
