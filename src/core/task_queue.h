#pragma once

#include "core/channel.h"

namespace dap_dbgeng::core
{
// Single-consumer task executor built on `channel`. Producers post work from
// any thread; one worker thread calls run() and executes tasks in order. This
// is how the DAP thread marshals operations onto the dbgeng worker thread,
// which alone may touch the debug engine.
class task_queue
{
  public:
    using task = std::function<void()>;

    // Push-and-forget: enqueue work to run on the worker thread. Returns false
    // if the queue has been stopped.
    bool post(task work)
    {
        return channel_.push(std::move(work));
    }

    // Push-and-wait: enqueue work and return a future for its result. Block on
    // future.get() to retrieve the value (or rethrow the task's exception). If
    // the queue is stopped before the task runs, the future becomes ready with
    // a std::future_error (broken promise) rather than hanging forever.
    template <class F> auto submit(F &&function) -> std::future<std::invoke_result_t<F>>
    {
        using result_t = std::invoke_result_t<F>;
        auto packaged = std::make_shared<std::packaged_task<result_t()>>(std::forward<F>(function));
        std::future<result_t> future = packaged->get_future();
        post([packaged] { (*packaged)(); });
        return future;
    }

    // Worker loop: drain and execute tasks until stop() is called and the queue
    // is empty. Run this on the dedicated worker thread.
    void run()
    {
        while (std::optional<task> work = channel_.pop())
        {
            (*work)();
        }
    }

    // Stop the worker: closes the channel so run() returns once drained.
    void stop()
    {
        channel_.close();
    }

  private:
    channel<task> channel_;
};
} // namespace dap_dbgeng::core
