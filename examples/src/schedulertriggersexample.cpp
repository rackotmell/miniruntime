#include "miniruntime/event/eventloop.h"
#include "miniruntime/logger/logger.h"
#include "miniruntime/scheduler/taskscheduler.h"

#include <atomic>
#include <thread>

int main()
{
    using namespace miniruntime::event;
    using namespace miniruntime::logger;
    using namespace miniruntime::scheduler;
    using namespace std::chrono_literals;

    Logger::getInstance().setMinLevel(LogLevel::Info);

    TaskScheduler scheduler;
    scheduler.init();

    // One-shot timer (schedule): run once after a delay.
    auto oneShot = scheduler.schedule(400ms, [] {
        LOG_INFO("timer: one-shot fired");
        return 42;
    });
    if (auto value = oneShot.get()) {
        LOG_INFO("timer: one-shot result = {}", *value);
    }

    // Interval (scheduleInterval): run repeatedly until cancelled.
    auto interval = scheduler.scheduleInterval(200ms, [] {
        LOG_INFO("interval: tick");
    });

    std::this_thread::sleep_for(1s);
    scheduler.cancel(interval->getId());
    LOG_INFO("interval: cancelled");
    interval->wait(); // unblocks immediately once closed

    // EventLoop trigger feeds tasks into the scheduler on demand.
    EventLoop loop;
    std::jthread loopThread([&loop] { loop.run(); });

    std::atomic<int> triggerCount{0};
    TriggerHandle trigger = loop.createTrigger([&scheduler, &triggerCount] {
        scheduler.execute([&triggerCount] {
            ++triggerCount;
            LOG_INFO("trigger: pool task #{} ran", triggerCount.load());
        });
    });

    for (int i = 0; i < 3; ++i) {
        trigger.trigger(); // wake the loop from any thread
        std::this_thread::sleep_for(150ms);
    }
    while (triggerCount.load() < 3) {
        std::this_thread::sleep_for(10ms);
    }

    loop.stop();
    loopThread.join();

    scheduler.stop();
}
