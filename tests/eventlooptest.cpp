#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "eventloop.h"

using namespace miniruntime;
using namespace std::chrono_literals;

namespace
{

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

TEST(EventLoopTest, TriggerFiresCallback)
{
    EventLoop loop;
    std::atomic<bool> fired{false};

    TriggerHandle trigger = loop.createTrigger([&fired] { fired.store(true); });
    LoopRunner runner(loop, 1s);

    trigger.trigger();
    EXPECT_TRUE(runner.waitFor([&fired] { return fired.load(); }));
}

TEST(EventLoopTest, TimerFiresCallback)
{
    EventLoop loop;
    std::atomic<bool> ran{false};

    TimerHandle timer = loop.createTimer(30ms, [&ran] { ran.store(true); });

    LoopRunner runner(loop, 1s);
    EXPECT_TRUE(runner.waitFor([&ran] { return ran.load(); }));
    EXPECT_TRUE(timer.fired());
}

TEST(EventLoopTest, TimerCancelPreventsFire)
{
    EventLoop loop;
    std::atomic<bool> ran{false};

    TimerHandle timer = loop.createTimer(100ms, [&ran] { ran.store(true); });
    timer.cancel();
    EXPECT_FALSE(timer.valid());
    EXPECT_FALSE(timer.fired());

    LoopRunner runner(loop, 200ms);
    EXPECT_FALSE(runner.waitFor([&ran] { return ran.load(); }));
}

TEST(EventLoopTest, TimerDestroyedWithoutFiring)
{
    EventLoop loop;
    std::atomic<bool> ran{false};

    {
        TimerHandle timer = loop.createTimer(100ms, [&ran] { ran.store(true); });
        ASSERT_TRUE(timer.valid());
    }
    // Destruction unregisters the timer, so it must never fire.

    LoopRunner runner(loop, 200ms);
    EXPECT_FALSE(runner.waitFor([&ran] { return ran.load(); }));
}

TEST(EventLoopTest, IntervalFiresRepeatedly)
{
    EventLoop loop;
    std::atomic<int> ticks{0};

    IntervalHandle interval = loop.createInterval(20ms, [&ticks] { ticks.fetch_add(1); });

    LoopRunner runner(loop, 1s);
    EXPECT_TRUE(runner.waitFor([&ticks] { return ticks.load() >= 3; }));
}

TEST(EventLoopTest, IntervalCancelStopsTicks)
{
    EventLoop loop;
    std::atomic<int> ticks{0};

    IntervalHandle interval = loop.createInterval(100ms, [&ticks] { ticks.fetch_add(1); });
    LoopRunner runner(loop, 1s);

    ASSERT_TRUE(runner.waitFor([&ticks] { return ticks.load() >= 1; }));

    const auto before = ticks.load();
    interval.cancel();
    EXPECT_FALSE(interval.valid());

    std::this_thread::sleep_for(500ms);
    EXPECT_EQ(ticks.load(), before);
}

TEST(EventLoopTest, IntervalResetAppliesImmediately)
{
    EventLoop loop;
    std::atomic<bool> ran{false};

    IntervalHandle interval = loop.createInterval(10s, [&ran] { ran.store(true); });
    interval.resetInterval(20ms);

    LoopRunner runner(loop, 1s);
    EXPECT_TRUE(runner.waitFor([&ran] { return ran.load(); }));
}

TEST(EventLoopTest, RawFdEvent)
{
    EventLoop loop;
    std::atomic<bool> ran{false};

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    EventHandle event =
        loop.createEvent(fds[0], EPOLLIN, EventType::SOCKET, [&ran](int) { ran.store(true); });
    ASSERT_TRUE(event.valid());

    LoopRunner runner(loop, 1s);

    ASSERT_EQ(write(fds[1], "x", 1), 1);
    EXPECT_TRUE(runner.waitFor([&ran] { return ran.load(); }));

    close(fds[0]);
    close(fds[1]);
}

TEST(EventLoopTest, MovedTriggerInvalidatesSource)
{
    EventLoop loop;
    std::atomic<int> fired{0};

    TriggerHandle trigger = loop.createTrigger([&fired] { fired.fetch_add(1); });
    TriggerHandle moved = std::move(trigger);

    EXPECT_FALSE(trigger.valid());
    EXPECT_TRUE(moved.valid());

    LoopRunner runner(loop, 1s);

    moved.trigger();
    // Triggering a moved-from handle must be a no-op.
    trigger.trigger();

    EXPECT_TRUE(runner.waitFor([&fired] { return fired.load() == 1; }));
}

TEST(EventLoopTest, MovedTimerInvalidatesSource)
{
    EventLoop loop;
    std::atomic<int> fired{0};

    TimerHandle timer = loop.createTimer(800ms, [&fired] { fired.fetch_add(1); });
    TimerHandle moved = std::move(timer);

    EXPECT_FALSE(timer.valid());
    EXPECT_TRUE(moved.valid());

    // Cancel a moved-from timer must be a no-op.
    timer.cancel();

    LoopRunner runner(loop, 1s);
    EXPECT_TRUE(runner.waitFor([&fired] { return fired.load() == 1; }));
}

TEST(EventLoopTest, MovedIntervalInvalidatesSource)
{
    EventLoop loop;
    std::atomic<int> fired{0};

    IntervalHandle interval = loop.createInterval(400ms, [&fired] { fired.fetch_add(1); });
    IntervalHandle moved = std::move(interval);

    EXPECT_FALSE(interval.valid());
    EXPECT_TRUE(moved.valid());

    // Cancel or resetInterval a moved-from interval must be a no-op.
    interval.cancel();
    interval.resetInterval(20s);

    LoopRunner runner(loop, 1s);
    EXPECT_TRUE(runner.waitFor([&fired] { return fired.load() == 2; }));
}
