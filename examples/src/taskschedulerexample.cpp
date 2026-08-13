#include "miniruntime/logger/logger.h"
#include "miniruntime/scheduler/taskscheduler.h"

#include <thread>

int main()
{
    using namespace miniruntime::logger;
    using namespace miniruntime::scheduler;
    using namespace std::chrono_literals;

    Logger::getInstance().setMinLevel(LogLevel::Info);

    TaskScheduler scheduler;
    scheduler.init();

    // Fire-and-forget task on the thread pool.
    scheduler.execute([] { LOG_INFO("execute: task ran immediately"); });

    // Run once after a delay, get the result via Future.
    auto future = scheduler.schedule(
        500ms,
        [](int a, int b) {
            LOG_INFO("schedule: computing {} + {}", a, b);
            return a + b;
        },
        2,
        3);

    if (auto result = future.get()) {
        LOG_INFO("schedule: future result = {}", *result);
    }

    // Run repeatedly until cancelled by id.
    auto shared = scheduler.scheduleInterval(250ms, [] { LOG_INFO("scheduleInterval: tick"); });

    std::this_thread::sleep_for(1s);
    scheduler.cancel(shared->getId());
    LOG_INFO("scheduleInterval: cancelled; the final wait() returns immediately");

    // Cancel a pending one-shot timer before it fires.
    auto pending =
        scheduler.schedule(10s, [] { LOG_INFO("schedule: this should never be logged"); });
    if (scheduler.cancel(pending.getId())) {
        LOG_INFO("schedule: pending timer cancelled by id");
    }

    scheduler.stop();
}
