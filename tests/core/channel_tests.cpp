#include <gtest/gtest.h>

#include "core/channel.h"
#include "core/event_sink.h"
#include "core/task_queue.h"

namespace
{
using dap_dbgeng::core::channel;
using dap_dbgeng::core::event_sink;
using dap_dbgeng::core::task_queue;

TEST(Channel, PushPopIsFifo)
{
    channel<int> ch;
    EXPECT_TRUE(ch.push(1));
    EXPECT_TRUE(ch.push(2));
    EXPECT_EQ(ch.pop(), 1);
    EXPECT_EQ(ch.pop(), 2);
}

TEST(Channel, TryPopReturnsFalseWhenEmpty)
{
    channel<int> ch;
    int value = 0;
    EXPECT_FALSE(ch.try_pop(value));

    ch.push(42);
    EXPECT_TRUE(ch.try_pop(value));
    EXPECT_EQ(value, 42);
}

TEST(Channel, CloseDrainsRemainingThenReturnsNullopt)
{
    channel<int> ch;
    ch.push(7);
    ch.close();

    EXPECT_TRUE(ch.is_closed());
    EXPECT_EQ(ch.pop(), 7);            // queued item still delivered
    EXPECT_EQ(ch.pop(), std::nullopt); // then closed + drained
}

TEST(Channel, PushAfterCloseIsRejected)
{
    channel<int> ch;
    ch.close();
    EXPECT_FALSE(ch.push(1));
}

TEST(Channel, CloseWakesBlockedConsumer)
{
    channel<int> ch;
    std::thread consumer([&] { EXPECT_EQ(ch.pop(), std::nullopt); });
    ch.close();
    consumer.join();
}

TEST(TaskQueue, PostRunsWorkOnWorkerThread)
{
    task_queue queue;
    std::thread worker([&] { queue.run(); });

    std::promise<std::thread::id> where;
    std::future<std::thread::id> ran_on = where.get_future();
    queue.post([&] { where.set_value(std::this_thread::get_id()); });

    EXPECT_EQ(ran_on.get(), worker.get_id());

    queue.stop();
    worker.join();
}

TEST(TaskQueue, SubmitReturnsResult)
{
    task_queue queue;
    std::thread worker([&] { queue.run(); });

    std::future<int> sum = queue.submit([] { return 2 + 3; });
    EXPECT_EQ(sum.get(), 5);

    queue.stop();
    worker.join();
}

TEST(TaskQueue, SubmitPropagatesException)
{
    task_queue queue;
    std::thread worker([&] { queue.run(); });

    std::future<void> result = queue.submit([]() -> void { throw std::runtime_error("boom"); });
    EXPECT_THROW(result.get(), std::runtime_error);

    queue.stop();
    worker.join();
}

TEST(TaskQueue, TasksRunInPostOrder)
{
    task_queue queue;
    std::vector<int> order;
    std::thread worker([&] { queue.run(); });

    for (int i = 0; i < 5; ++i)
    {
        queue.post([&order, i] { order.push_back(i); });
    }
    queue.submit([] {}).get(); // barrier: wait for all prior tasks to drain

    queue.stop();
    worker.join();

    EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));
}

// A simple in-memory event_sink, like how the transport will capture and
// forward dbgeng-originated DAP events.
class capturing_sink : public event_sink
{
  public:
    void emit(const nlohmann::json &event) override
    {
        events.push_back(event);
    }

    std::vector<nlohmann::json> events;
};

TEST(EventSink, CapturesEmittedEventsThroughInterface)
{
    capturing_sink sink;
    event_sink &as_interface = sink;

    as_interface.emit({{"type", "event"}, {"event", "stopped"}});

    ASSERT_EQ(sink.events.size(), 1u);
    EXPECT_EQ(sink.events[0]["event"], "stopped");
}
} // namespace
