#include <atomic>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

#include "taskscheduler.h"

using namespace miniruntime;
using namespace std::chrono_literals;

namespace
{

bool waitFor(std::function<bool()> predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

} // namespace

TEST(TaskSchedulerTest, ExecuteReturnsResultWithArgs)
{
    TaskScheduler scheduler(1, 2);

    auto future = scheduler.execute([](int a, int b) { return a + b; }, 2, 3);
    ASSERT_TRUE(future.get().has_value());
    EXPECT_EQ(future.get().value(), 5);
}

TEST(TaskSchedulerTest, ExecuteVoidTask)
{
    TaskScheduler scheduler(1, 2);
    std::atomic<bool> ran{false};

    scheduler.execute([&ran] { ran.store(true); });

    EXPECT_TRUE(waitFor([&ran] { return ran.load(); }, 1s));
}

TEST(TaskSchedulerTest, ExecutePropagatesException)
{
    TaskScheduler scheduler(1, 2);

    auto future = scheduler.execute([] { throw std::runtime_error("boom"); });
    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(TaskSchedulerTest, ScheduleRunsAfterDelay)
{
    TaskScheduler scheduler(1, 2);
    scheduler.init();

    auto future = scheduler.schedule(50ms, [] { return 42; });

    EXPECT_FALSE(future.isReady());
    
    ASSERT_TRUE(waitFor([&]{ return future.isReady(); }, 1s));
    ASSERT_TRUE(future.get().has_value());
    EXPECT_EQ(future.get().value(), 42);
}

TEST(TaskSchedulerTest, SchedulePropagatesException)
{
    TaskScheduler scheduler(1, 2);
    scheduler.init();

    auto future = scheduler.schedule(50ms, [] { throw std::logic_error("scheduled failure"); });

    ASSERT_TRUE(waitFor([&]{ return future.isReady(); }, 1s));
    EXPECT_THROW(future.get(), std::logic_error);
}

TEST(TaskSchedulerTest, CancelPendingTimerClosesFuture)
{
    TaskScheduler scheduler(1, 2);
    scheduler.init();

    auto future = scheduler.schedule(30s, [] { return 1; });
    auto id = future.getId();
    ASSERT_TRUE(id.has_value());

    EXPECT_TRUE(scheduler.cancel(id));
    EXPECT_FALSE(future.get().has_value());
}

TEST(TaskSchedulerTest, CancelUnknownIdReturnsFalse)
{
    TaskScheduler scheduler(1, 2);
    scheduler.init();

    EXPECT_FALSE(scheduler.cancel(123456));
}

TEST(TaskSchedulerTest, IntervalTicksUntilCancelled)
{
    TaskScheduler scheduler(1, 2);
    scheduler.init();

    std::atomic<int> ticks{0};
    auto shared = scheduler.scheduleInterval(20ms, [&ticks] { ticks.fetch_add(1); });

    EXPECT_TRUE(waitFor([&ticks] { return ticks.load() >= 3; }, 2s));

    auto id = shared->getId();
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(scheduler.cancel(id));

    const auto after = ticks.load();
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(ticks.load(), after);

    EXPECT_FALSE(shared->wait());
}

TEST(TaskSchedulerTest, IntervalCarriesLatestValue)
{
    TaskScheduler scheduler(1, 2);
    scheduler.init();

    std::atomic<int> tick{0};
    auto shared = scheduler.scheduleInterval(100ms, [&tick] { return tick.fetch_add(1); });

    auto first = shared->wait();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first.value(), 0);

    auto second = shared->wait();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second.value(), 1);

    auto id = shared->getId();
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(scheduler.cancel(id));
}

TEST(TaskSchedulerTest, InitIsIdempotent)
{
    TaskScheduler scheduler(1, 2);
    scheduler.init();
    EXPECT_NO_THROW(scheduler.init());
    scheduler.stop();
}
