#include "eventloop.h"
#include "handle.h"
#include "logger.h"
#include "taskscheduler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

// Cheap "processing" to run on the scheduler's thread pool.
static std::string processLine(const std::string& line)
{
    std::string result = line;
    std::transform(result.begin(), result.end(), result.begin(), toupper);
    return result;
}

int main()
{
    using namespace std::chrono_literals;
    using namespace miniruntime;

    Logger::getInstance().setMinLevel(LogLevel::Info);

    // TaskSchedulerused to process socket data.
    TaskScheduler scheduler;
    scheduler.init();

    // The EventLoop (not exposed by TaskScheduler) watches the server sockets;
    EventLoop loop;
    std::jthread loopThread([&loop] { loop.run(); });

    // A connected pair: the server side is watched by the EventLoop,
    // the client side is driven by a plain thread below.
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        LOG_ERROR("socketpair failed: {}", std::strerror(errno));
        return 1;
    }
    const int serverFd = sockets[0];
    const int clientFd = sockets[1];

    constexpr int TOTAL_MESSAGES = 5;
    std::atomic<int> processed{0};

    // Server: recieves client messages, sends processed reply.
    EventHandle serverHandle = loop.createEvent(
        serverFd, EPOLLIN, EventType::SOCKET, [&scheduler, serverFd, &processed](int) {
            std::array<char, 256> buffer;
            const ssize_t n = recv(serverFd, buffer.data(), buffer.size(), 0);
            if (n <= 0) return;

            const std::string line(buffer.data(), static_cast<size_t>(n));
            scheduler.execute([line, serverFd, &processed] {
                const std::string result = processLine(line);
                send(serverFd, result.data(), result.size(), MSG_NOSIGNAL);
                ++processed;
                LOG_INFO("Server socket: '{}' -> '{}'", line, result);
            });
        });

    // External client: send a line, block until the processed reply arrives.
    std::jthread clientThread([clientFd] {
        std::string reply;
        reply.resize(256);
        for (int i = 1; i <= TOTAL_MESSAGES; ++i) {
            const std::string msg = "message " + std::to_string(i);
            send(clientFd, msg.data(), msg.size(), MSG_NOSIGNAL);

            const ssize_t n = recv(clientFd, reply.data(), reply.size(), 0);
            if (n > 0) {
                LOG_INFO("Client socket: client got '{}'", std::string(reply.data(), n));
            }
        }
        close(clientFd);
    });

    clientThread.join();

    loop.stop();
    loopThread.join();

    close(serverFd);

    scheduler.stop();
}
