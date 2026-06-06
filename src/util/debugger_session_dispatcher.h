#pragma once

#include "core/task_queue.h"

namespace dap_dbgeng::util
{
// Serializes debugger-session operations onto a single dedicated background
// thread. dbgeng has thread affinity, so every debugger_session call must run on
// the one worker thread that owns it.
//
// `invoke` runs the callable on the worker thread and blocks for its result,
// rethrowing any exception (wrapped). When already on the worker thread it
// short-circuits and calls directly (the dispatcher thread itself runs the
// event callbacks, which may re-enter session calls). The worker
// thread starts in the constructor and is stopped and joined in the destructor.
class debugger_session_dispatcher
{
  public:
    debugger_session_dispatcher();
    ~debugger_session_dispatcher();

    debugger_session_dispatcher(const debugger_session_dispatcher &) = delete;
    debugger_session_dispatcher &operator=(const debugger_session_dispatcher &) = delete;

    // True when the caller is already running on the dispatcher worker thread
    // (i.e. inside an engine event callback). Lets re-entrant teardown defer the
    // actual COM destruction off the callback stack rather than destroying the
    // engine synchronously inside one of its own callbacks.
    bool is_on_worker_thread() const
    {
        return std::this_thread::get_id() == worker_id_.load();
    }

    // Run `function` on the dispatcher thread and return its result. Exceptions
    // thrown on the worker thread are propagated to the caller (wrapped in a
    // std::runtime_error for consistent error mapping).
    template <class F> auto invoke(F &&function) -> std::invoke_result_t<F>
    {
        using result_t = std::invoke_result_t<F>;

        // Short-circuit when already on the worker thread (event callbacks fire
        // there and may re-enter session calls).
        if (std::this_thread::get_id() == worker_id_.load())
        {
            return function();
        }

        std::future<result_t> future = queue_.submit(std::forward<F>(function));
        try
        {
            return future.get();
        }
        catch (const std::exception &exception)
        {
            // Wrap so callers can recognize a dispatcher-thread failure,
            // preserving the inner message.
            throw dispatcher_failure(exception.what());
        }
    }

  private:
    // Exception type carrying the dispatcher-failure message plus the inner
    // text so the evaluate/setVariable handlers can unwrap it.
    class dispatcher_failure : public std::runtime_error
    {
      public:
        explicit dispatcher_failure(std::string inner_message)
            : std::runtime_error("A debugger session operation failed on the dispatcher thread."),
              inner_message_(std::move(inner_message))
        {
        }

        const std::string &inner_message() const
        {
            return inner_message_;
        }

      private:
        std::string inner_message_;
    };

  public:
    // Returns the inner message of a dispatcher failure (the underlying engine
    // error), or `fallback` if `exception` is not a dispatcher failure. Used by
    // the evaluate/setVariable handlers to surface the real engine error.
    static std::string unwrap_failure_message(const std::exception &exception, const std::string &fallback);

  private:
    core::task_queue queue_;
    std::atomic<std::thread::id> worker_id_;
    std::thread worker_;
};
} // namespace dap_dbgeng::util
