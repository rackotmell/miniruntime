#include "boundedblockingqueue.h"
#include "eventloop.h"
#include "logger.h"

#include <atomic>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// Producer/consumer without TaskScheduler: a plain thread pushes items
// into a BoundedBlockingQueue, an EventLoop interval drains it.
int main()
{
    using namespace miniruntime;

    Logger::getInstance().setMinLevel(LogLevel::Info);

    BoundedBlockingQueue<std::string> queue(8);
    EventLoop loop;
    std::jthread loopThread([&loop] { loop.run(); });

    constexpr int TOTAL = 6;
    std::atomic<int> consumed{0};

    // The loop consumes one item per tick; timeoutPop(0) never blocks.
    IntervalHandle interval = loop.createInterval(100ms, [&queue, &consumed] {
        if (auto item = queue.timeoutPop(0ms)) {
            consumed.fetch_add(1);
            LOG_INFO("loop: consumed '{}' ({}/{})", *item, consumed.load(), TOTAL);
        }
    });

    // Producer thread feeds the queue.
    std::jthread producer([&queue] {
        for (int i = 1; i <= TOTAL; ++i) {
            queue.push("item " + std::to_string(i));
            std::this_thread::sleep_for(120ms);
        }
    });

    producer.join();
    loop.stop();
    loopThread.join();
}
