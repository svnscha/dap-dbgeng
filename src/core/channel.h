#pragma once

namespace dap_dbgeng::core
{
// Thread-safe FIFO used to hand work between threads (e.g. the DAP protocol
// thread and the dbgeng worker thread). `pop()` blocks until an item is
// available or the channel is closed; `close()` wakes every waiter so a
// consumer loop can exit cleanly on shutdown.
template <class T> class channel
{
  public:
    // Enqueue an item. Returns false if the channel is already closed (the item
    // is dropped), true otherwise.
    bool push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_)
            {
                return false;
            }
            queue_.push_back(std::move(value));
        }
        condition_.notify_one();
        return true;
    }

    // Block until an item is available or the channel is closed. Returns the
    // next item, or std::nullopt once the channel is closed *and* drained.
    std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty())
        {
            return std::nullopt; // closed and drained
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    // Non-blocking pop. Returns false if no item is currently available.
    bool try_pop(T &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty())
        {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    // Close the channel and wake all waiters. Already-queued items remain
    // available to pop() until drained.
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<T> queue_;
    bool closed_ = false;
};
} // namespace dap_dbgeng::core
