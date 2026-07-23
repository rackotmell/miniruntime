#include <chrono>
#include <iostream>
#include <ostream>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

#include "eventloop.h"
#include "threadpool.h"

int main() {

    miniruntime::EventLoop loop;
    std::thread eventLoopThreadloop([&loop] { loop.run(); });

    {
        miniruntime::ThreadPool pool;

        auto trigger = loop.createTrigger([&pool]{
            pool.enqueue([]{
                std::cout << "Triggered1" << std::flush;
            });
            pool.enqueue([]{
                std::cout << "Triggered2" << std::flush;
            });
        });

        trigger.trigger();

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    loop.stop();

    eventLoopThreadloop.join();
}