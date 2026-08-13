#include "miniruntime/task/dynamicthreadpool.h"
#include "miniruntime/event/eventloop.h"
#include "miniruntime/logger/logger.h"

#include <atomic>
#include <chrono>

// Raw building blocks, no TaskScheduler: an EventLoop interval timer
// feeds work into a DynamicThreadPool.
int main()
{
    using namespace miniruntime::event;
    using namespace miniruntime::logger;
    using namespace miniruntime::task;
    using namespace std::chrono_literals;

    Logger::getInstance().setMinLevel(LogLevel::Info);

    DynamicThreadPool pool(2, 4, 1s, 64);
    EventLoop loop;
    std::jthread loopThread([&loop] { loop.run(); });

    constexpr int TICKS = 5;
    std::atomic<int> tickCount{0};

    IntervalHandle interval = loop.createInterval(150ms, [&loop, &pool, &tickCount] {
        const int tick = tickCount.fetch_add(1) + 1;
        LOG_INFO("loop: interval tick #{}", tick);
        pool.enqueue([tick] {
            std::this_thread::sleep_for(50ms);
            LOG_INFO("pool: task from tick #{} on {}", tick, std::this_thread::get_id());
        });
        if (tick >= TICKS) loop.stop();
    });

    loopThread.join();
}
