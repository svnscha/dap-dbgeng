#include "util/debugger_session_dispatcher.h"

namespace dap_dbgeng::util
{
debugger_session_dispatcher::debugger_session_dispatcher()
{
    // Start the worker and publish its id before any invoke() can race the
    // short-circuit check (the promise blocks the ctor until the id is set).
    std::promise<std::thread::id> started;
    std::future<std::thread::id> ready = started.get_future();
    worker_ = std::thread([this, &started] {
        started.set_value(std::this_thread::get_id());
        queue_.run();
    });
    worker_id_.store(ready.get());
}

debugger_session_dispatcher::~debugger_session_dispatcher()
{
    queue_.stop();
    if (worker_.joinable())
    {
        worker_.join();
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
