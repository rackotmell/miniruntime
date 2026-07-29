#include "logger.h"
#include "taskscheduler.h"
#include <chrono>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    
    miniruntime::TaskScheduler scheduler;
    scheduler.init();

    scheduler.execute([] {
        LOG_DEBUG("TaskScheduler execute called");
    });

    auto future = scheduler.schedule(std::chrono::seconds(5), [] {
        LOG_DEBUG("TaskScheduler execute called");
        return 2;
    });

    auto val = future.get();
    LOG_DEBUG("TaskScheduler schedule called. Future result={}", val);

    auto shared = scheduler.scheduleInterval(std::chrono::seconds(2), [] {
        LOG_DEBUG("TaskScheduler schedule interval called");
    });

    for (int i = 0; i < 4; ++i) {
        auto val2 = shared->wait();
        if (val2)
            LOG_DEBUG("TaskScheduler schedule interval called. Future result={}", val2);
    }

    scheduler.cancel(shared->getId());

    std::this_thread::sleep_for(5s);

    scheduler.stop();
}