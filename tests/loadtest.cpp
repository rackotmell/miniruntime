#include <atomic>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

#include "boundedblockingqueue.h"
#include "dynamicthreadpool.h"
#include "eventloop.h"
#include "future.h"
#include "taskscheduler.h"

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

// Runs the loop until the predicate holds or the timeout elapses.
// The loop is stopped and its thread joined before returning.
class LoopRunner
{
public:
    explicit LoopRunner(EventLoop& loop, std::chrono::milliseconds timeout)
        : m_loop(loop), m_timeout(timeout), m_thread([&] { m_loop.run(); })
    {
    }

    bool waitFor(std::function<bool()> predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + m_timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) {
                m_loop.stop();
                m_thread.join();
                return true;
            }
            std::this_thread::sleep_for(5ms);
        }
        m_loop.stop();
        m_thread.join();
        return predicate();
    }

    ~LoopRunner() = default;

private:
    EventLoop& m_loop;
    std::chrono::milliseconds m_timeout;
    std::thread m_thread;
};

} // namespace

// ============================================================
// BoundedBlockingQueue load
// ============================================================

TEST(LoadTest, QueueManyProducersManyConsumers)
{
    constexpr int producersCount = 4;
    constexpr int perProducer = 20000;
    constexpr int consumersCount = 4;
    BoundedBlockingQueue<int> queue(64);

    std::vector<std::atomic<int>> seen(producersCount * perProducer);
    for (auto& value : seen)
        value.store(0);

    std::atomic<bool> failed{false};

    std::vector<std::thread> producers;
    producers.reserve(producersCount);
    for (int p = 0; p < producersCount; ++p) {
        producers.emplace_back([&queue, &failed, p] {
            for (int i = 0; i < perProducer; ++i) {
                if (!queue.push(p * perProducer + i)) {
                    failed.store(true);
                    return;
                }
            }
        });
    }

    std::vector<std::thread> consumers;
    consumers.reserve(consumersCount);
    for (int c = 0; c < consumersCount; ++c) {
        consumers.emplace_back([&queue, &seen, &failed] {
            while (auto value = queue.pop()) {
                const int v = value.value();
                if (v < 0 || v >= static_cast<int>(seen.size())) {
                    failed.store(true);
                    continue;
                }
                seen[v].fetch_add(1);
            }
        });
    }

    for (auto& thread : producers)
        thread.join();
    queue.close();
    for (auto& thread : consumers)
        thread.join();

    EXPECT_FALSE(failed.load());
    for (const auto& value : seen) {
        EXPECT_EQ(value.load(), 1);
    }
}

TEST(LoadTest, QueueBackpressureWithFullCapacity)
{
    constexpr int count = 10000;
    BoundedBlockingQueue<int> queue(2);

    std::atomic<int> consumed{0};
    std::atomic<bool> failed{false};

    std::thread consumer([&] {
        while (auto value = queue.pop()) {
            consumed.fetch_add(1);
            (void) value;
        }
    });

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            if (!queue.push(i)) {
                failed.store(true);
                return;
            }
        }
        queue.close();
    });

    producer.join();
    consumer.join();

    EXPECT_FALSE(failed.load());
    EXPECT_EQ(consumed.load(), count);
}

// ============================================================
// DynamicThreadPool load
// ============================================================

TEST(LoadTest, ThreadPoolHighThroughputFromManyProducers)
{
    DynamicThreadPool pool(4, 16, 100ms, 1024);
    constexpr int producers = 4;
    constexpr int perProducer = 5000;
    constexpr int total = producers * perProducer;
    std::atomic<int> counter{0};

    std::vector<std::thread> threads;
    threads.reserve(producers);
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&pool, &counter] {
            for (int i = 0; i < perProducer; ++i) {
                pool.enqueue([&counter] { counter.fetch_add(1); });
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_TRUE(waitFor([&] { return counter.load() == total; }, 30s));
}

TEST(LoadTest, ThreadPoolScalesUpUnderBurst)
{
    DynamicThreadPool pool(2, 16, 200ms, 256);
    constexpr int count = 64;
    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
    std::atomic<int> done{0};

    for (int i = 0; i < count; ++i) {
        pool.enqueue([&] {
            const int now = active.fetch_add(1) + 1;
            int observed = maxActive.load();
            while (now > observed && !maxActive.compare_exchange_weak(observed, now)) {
            }
            std::this_thread::sleep_for(5ms);
            active.fetch_sub(1);
            done.fetch_add(1);
        });
    }

    EXPECT_TRUE(waitFor([&] { return done.load() == count; }, 10s));
    // The pool must have grown beyond the minPoolSize floor of 2 workers.
    EXPECT_GT(maxActive.load(), 2);
}

TEST(LoadTest, ThreadPoolNoTaskLossWithMixedExceptions)
{
    DynamicThreadPool pool(4, 8, 100ms, 256);
    constexpr int count = 5000;
    std::atomic<int> ok{0};
    std::atomic<int> failed{0};

    for (int i = 0; i < count; ++i) {
        if (i % 10 == 0) {
            pool.enqueue([&failed] {
                failed.fetch_add(1);
                throw std::runtime_error("boom");
            });
        } else {
            pool.enqueue([&ok] { ok.fetch_add(1); });
        }
    }

    EXPECT_TRUE(waitFor([&] { return ok.load() + failed.load() == count; }, 10s));
    EXPECT_EQ(ok.load(), count - (count / 10));
}

// ============================================================
// Future/Promise load
// ============================================================

TEST(LoadTest, FutureManyConcurrentSetAndGet)
{
    constexpr int count = 2000;
    constexpr int workers = 4;
    const int chunk = count / workers;

    std::vector<Promise<int>> promises(count);
    std::vector<Future<int>> futures;
    futures.reserve(count);
    for (int i = 0; i < count; ++i) {
        futures.push_back(promises[i].getFuture());
    }

    std::atomic<int> consumed{0};
    std::atomic<bool> failed{false};

    std::vector<std::thread> consumers;
    consumers.reserve(workers);
    for (int w = 0; w < workers; ++w) {
        consumers.emplace_back([&, w] {
            for (int i = w * chunk; i < (w + 1) * chunk; ++i) {
                auto value = futures[i].get();
                if (!value.has_value() || value.value() != i) failed.store(true);
                consumed.fetch_add(1);
            }
        });
    }

    std::vector<std::thread> producers;
    producers.reserve(workers);
    for (int w = 0; w < workers; ++w) {
        producers.emplace_back([&, w] {
            for (int i = w * chunk; i < (w + 1) * chunk; ++i) {
                promises[i].setValue(i);
            }
        });
    }

    for (auto& thread : producers)
        thread.join();
    for (auto& thread : consumers)
        thread.join();

    EXPECT_EQ(consumed.load(), count);
    EXPECT_FALSE(failed.load());
}

// ============================================================
// TaskScheduler load
// ============================================================

TEST(LoadTest, SchedulerExecuteResultsUnderLoad)
{
    TaskScheduler scheduler(4, 8);

    constexpr int count = 2000;
    std::vector<Future<int>> futures;
    futures.reserve(count);

    for (int i = 0; i < count; ++i) {
        futures.push_back(scheduler.execute([](int a, int b) { return a + b; }, i, 1));
    }

    int sum = 0;
    for (auto& future : futures) {
        auto value = future.get();
        ASSERT_TRUE(value.has_value());
        sum += value.value();
    }
    EXPECT_EQ(sum, count * (count + 1) / 2);
}

TEST(LoadTest, SchedulerExecuteFromManyThreads)
{
    TaskScheduler scheduler(4, 8);
    constexpr int producers = 4;
    constexpr int perProducer = 1000;
    constexpr int total = producers * perProducer;
    std::atomic<int> counter{0};

    std::vector<std::thread> threads;
    threads.reserve(producers);
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&scheduler, &counter] {
            for (int i = 0; i < perProducer; ++i) {
                auto future = scheduler.execute([&counter] {
                    counter.fetch_add(1);
                    return 1;
                });
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_TRUE(waitFor([&] { return counter.load() == total; }, 30s));
}

TEST(LoadTest, SchedulerScheduleManyDelayedTasks)
{
    TaskScheduler scheduler(4, 8);
    scheduler.init();

    constexpr int count = 500;
    std::vector<Future<int>> futures;
    futures.reserve(count);

    for (int i = 0; i < count; ++i) {
        const auto delay = 10ms + std::chrono::milliseconds(i % 5);
        futures.push_back(scheduler.schedule(delay, [] { return 1; }));
    }

    int sum = 0;
    for (auto& future : futures) {
        sum += future.get().value_or(0);
    }
    EXPECT_EQ(sum, count);
}

TEST(LoadTest, SchedulerManyIntervalsTick)
{
    TaskScheduler scheduler(4, 8);
    scheduler.init();

    constexpr int intervalCount = 30;
    std::atomic<int> totalTicks{0};
    std::vector<std::shared_ptr<SharedValue<void>>> values;
    values.reserve(intervalCount);

    for (int i = 0; i < intervalCount; ++i) {
        values.push_back(
            scheduler.scheduleInterval(5ms, [&totalTicks] { totalTicks.fetch_add(1); }));
    }

    ASSERT_TRUE(waitFor([&] { return totalTicks.load() >= 150; }, 10s));

    for (auto& value : values) {
        auto id = value->getId();
        if (id) scheduler.cancel(id);
    }

    std::this_thread::sleep_for(100ms);
    const auto before = totalTicks.load();
    std::this_thread::sleep_for(200ms);
    const auto after = totalTicks.load();

    // All intervals were canceled 
    EXPECT_LE(before, after);
}

TEST(LoadTest, SchedulerMixedWorkload)
{
    TaskScheduler scheduler(4, 8);
    scheduler.init();

    std::atomic<int> executed{0};
    std::atomic<int> scheduled{0};
    std::atomic<int> ticked{0};

    constexpr int execCount = 500;
    std::vector<Future<int>> futures;
    futures.reserve(execCount);
    for (int i = 0; i < execCount; ++i) {
        futures.push_back(scheduler.execute([&executed] {
            executed.fetch_add(1);
            return 1;
        }));
    }

    constexpr int schCount = 200;
    for (int i = 0; i < schCount; ++i) {
        scheduler.schedule(10ms, [&scheduled] { scheduled.fetch_add(1); });
    }

    auto value = scheduler.scheduleInterval(5ms, [&ticked] { ticked.fetch_add(1); });

    int sum = 0;
    for (auto& future : futures) {
        sum += future.get().value_or(0);
    }
    EXPECT_EQ(sum, execCount);
    EXPECT_EQ(executed.load(), execCount);

    EXPECT_TRUE(waitFor([&] { return scheduled.load() == schCount; }, 5s));
    EXPECT_TRUE(waitFor([&] { return ticked.load() >= 20; }, 5s));

    auto id = value->getId();
    if (id) scheduler.cancel(id);
}

// ============================================================
// EventLoop load
// ============================================================

TEST(LoadTest, EventLoopManyTriggers)
{
    EventLoop loop;
    constexpr int count = 500;
    std::atomic<int> fired{0};

    std::vector<TriggerHandle> triggers;
    triggers.reserve(count);
    for (int i = 0; i < count; ++i) {
        triggers.push_back(loop.createTrigger([&fired] { fired.fetch_add(1); }));
    }

    LoopRunner runner(loop, 15s);

    for (auto& trigger : triggers)
        trigger.trigger();
    EXPECT_TRUE(runner.waitFor([&] { return fired.load() == count; }));
}

TEST(LoadTest, EventLoopManyTimers)
{
    EventLoop loop;
    constexpr int count = 500;
    std::atomic<int> fired{0};

    std::vector<TimerHandle> timers;
    timers.reserve(count);
    for (int i = 0; i < count; ++i) {
        timers.push_back(loop.createTimer(30ms, [&fired] { fired.fetch_add(1); }));
    }

    LoopRunner runner(loop, 15s);
    EXPECT_TRUE(runner.waitFor([&] { return fired.load() == count; }));
}

TEST(LoadTest, EventLoopManyIntervals)
{
    EventLoop loop;
    constexpr int count = 100;
    std::atomic<int> ticks{0};

    {
        std::vector<IntervalHandle> intervals;
        intervals.reserve(count);
        for (int i = 0; i < count; ++i) {
            intervals.push_back(loop.createInterval(5ms, [&ticks] { ticks.fetch_add(1); }));
        }

        LoopRunner runner(loop, 15s);
        EXPECT_TRUE(runner.waitFor([&] { return ticks.load() >= 300; }));
    }
}
