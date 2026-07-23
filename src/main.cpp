#include <iostream>
#include <ostream>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

#include "eventloop.h"

int main() {
    miniruntime::EventLoop loop;

    auto trigger = loop.createTrigger([]{
        std::cout << "Triggered" << std::flush;
    });

    std::thread eventLoopThreadloop([&loop] { loop.run(); });

    trigger.trigger();
    trigger.valid();

    const auto fd = eventfd(0, EFD_NONBLOCK);
    auto event = loop.createEvent(fd, EPOLLIN, miniruntime::EventType::TRIGGER,
        [](int fd){
            uint64_t val;
            read(fd, &val, sizeof(val));
            std::cout << "Evented" << std::flush;
    });

    event.valid();

    uint64_t one = 1;
    write(fd, &one, sizeof(one));

    eventLoopThreadloop.join();
}