#include <chrono>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

#include "eventloop.h"
#include "dynamicthreadpool.h"
#include "logger.h"

int main() {

    miniruntime::EventLoop loop;
    std::thread eventLoopThreadloop([&loop] { loop.run(); });

    {
        miniruntime::DynamicThreadPool pool;

        auto trigger = loop.createTrigger([&pool]{
            pool.enqueue([]{
                LOG_INFO("{}", "Pool task 1 executed");
            });
            pool.enqueue([]{
                LOG_INFO("{}", "Pool task 2 executed");
            });
        });

        trigger.trigger();

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    loop.stop();

    LOG_DEBUG("Event Loop stopped");

    eventLoopThreadloop.join();
}