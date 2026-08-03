#include <atomic>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <thread>

#include "dynamicthreadpool.h"

using namespace miniruntime;
using namespace std::chrono_literals;

namespace
{

// Waits until the predicate holds or the timeout elapses.
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

TEST(DynamicThreadPoolTest, ExecutesTask)
{
    DynamicThreadPool pool(1, 1, 1s, 8);
    std::atomic<bool> ran{false};

    pool.enqueue([&ran] { ran.store(true); });

    EXPECT_TRUE(waitFor([&ran] { return ran.load(); }, 1s));
}

TEST(DynamicThreadPoolTest, ExecutesManyTasks)
{
    DynamicThreadPool pool(2, 2, 1s, 64);
    constexpr int count = 200;
    std::atomic<int> counter{0};

    for (int i = 0; i < count; ++i) {
        pool.enqueue([&counter] { counter.fetch_add(1); });
    }

    EXPECT_TRUE(waitFor([&counter] { return counter.load() == count; }, 5s));
}

TEST(DynamicThreadPoolTest, RunsTasksConcurrently)
{
    DynamicThreadPool pool(2, 2, 1s, 8);
    std::atomic<bool> firstEntered{false};
    std::atomic<bool> secondEntered{false};

    pool.enqueue([&] {
        firstEntered.store(true);
        // Wait for the other task to start: deadlocks if a single worker runs both.
        EXPECT_TRUE(waitFor([&secondEntered] { return secondEntered.load(); }, 1s));
    });
    pool.enqueue([&] {
        secondEntered.store(true);
        EXPECT_TRUE(waitFor([&firstEntered] { return firstEntered.load(); }, 1s));
    });

    EXPECT_TRUE(waitFor([&] { return firstEntered.load() && secondEntered.load(); }, 1s));
}

TEST(DynamicThreadPoolTest, DontFallWithExceptionInOneTask)
{
    DynamicThreadPool pool(1, 1, 1s, 8);
    std::atomic<bool> ran{false};

    pool.enqueue([] { throw std::runtime_error("boom"); });
    pool.enqueue([&ran] { ran.store(true); });

    EXPECT_TRUE(waitFor([&ran] { return ran.load(); }, 1s));
}

TEST(DynamicThreadPoolTest, EnqueueAfterIdleTimeoutStillRuns)
{
    DynamicThreadPool pool(1, 2, 50ms, 8);
    std::atomic<bool> ran{false};

    pool.enqueue([&ran] {
        waitFor([] { return false; }, 200ms);
        ran.store(true);
    });

    // The worker must survive its idle timeout and set flag to true.
    EXPECT_TRUE(waitFor([&ran] { return ran.load(); }, 1s));
}
