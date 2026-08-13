#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "miniruntime/task/michaelscottqueue.h"

using namespace miniruntime::task;
using namespace std::chrono_literals;

TEST(MichaelScottQueueTest, PushPop)
{
    MichaelScottQueue<int> queue;

    ASSERT_TRUE(queue.push(1));
    ASSERT_TRUE(queue.push(2));
    EXPECT_EQ(queue.size(), 2);

    auto first = queue.pop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first.value(), 1);

    auto second = queue.pop();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second.value(), 2);
}

TEST(MichaelScottQueueTest, PushPopString)
{
    MichaelScottQueue<std::string> queue;

    ASSERT_TRUE(queue.push("Hello"));
    ASSERT_TRUE(queue.push("World"));

    auto first = queue.pop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first.value(), "Hello");

    auto second = queue.pop();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second.value(), "World");
}

TEST(MichaelScottQueueTest, Emplace)
{
    MichaelScottQueue<std::pair<int, std::string>> queue;

    queue.emplace(42, "answer");
    auto val = queue.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value().first, 42);
    EXPECT_EQ(val.value().second, "answer");
}

TEST(MichaelScottQueueTest, ClosePop)
{
    MichaelScottQueue<int> queue;
    queue.close();

    auto val = queue.pop();
    EXPECT_FALSE(val.has_value());
}

TEST(MichaelScottQueueTest, CloseRejectsPush)
{
    MichaelScottQueue<int> queue;
    queue.close();

    EXPECT_FALSE(queue.push(1));
}

TEST(MichaelScottQueueTest, CloseUnblocksPop)
{
    MichaelScottQueue<int> queue;

    std::thread consumer([&] {
        auto val = queue.pop();
        EXPECT_FALSE(val.has_value());
    });

    std::this_thread::sleep_for(10ms);
    queue.close();
    consumer.join();
}

TEST(MichaelScottQueueTest, TimeoutPopValue)
{
    MichaelScottQueue<int> queue;
    ASSERT_TRUE(queue.push(42));

    auto val = queue.timeoutPop(50ms);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 42);
}

TEST(MichaelScottQueueTest, TimeoutPopTimeout)
{
    MichaelScottQueue<int> queue;

    auto val = queue.timeoutPop(20ms);
    EXPECT_FALSE(val.has_value());
}

TEST(MichaelScottQueueTest, TimeoutPopClose)
{
    MichaelScottQueue<int> queue;

    std::thread closer([&] {
        std::this_thread::sleep_for(10ms);
        queue.close();
    });

    auto val = queue.timeoutPop(50ms);
    EXPECT_FALSE(val.has_value());
    closer.join();
}

TEST(MichaelScottQueueTest, ConcurrentSPSC)
{
    MichaelScottQueue<int> queue;
    constexpr int count = 50000;
    int received = 0;

    std::thread producer([&] {
        for (int i = 0; i < count; ++i)
            ASSERT_TRUE(queue.push(i));
        queue.close();
    });

    std::thread consumer([&] {
        while (auto val = queue.pop()) {
            EXPECT_EQ(val.value(), received);
            ++received;
        }
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(received, count);
}

TEST(MichaelScottQueueTest, ConcurrentMPMC)
{
    constexpr int numProducers = 4;
    constexpr int numConsumers = 4;
    constexpr int itemsPerProducer = 10000;

    MichaelScottQueue<int> queue;
    std::atomic<int> received{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < numProducers; ++p) {
        producers.emplace_back([&queue, p] {
            for (int i = 0; i < itemsPerProducer; ++i)
                queue.push(p * itemsPerProducer + i);
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < numConsumers; ++c) {
        consumers.emplace_back([&queue, &received] {
            while (auto val = queue.pop()) {
                received.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : producers) t.join();
    queue.close();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(received.load(), numProducers * itemsPerProducer);
}

TEST(MichaelScottQueueTest, CloseWhileMultipleBlocked)
{
    MichaelScottQueue<int> queue;
    constexpr int numConsumers = 4;
    std::atomic<int> received{0};

    std::vector<std::thread> consumers;
    for (int i = 0; i < numConsumers; ++i) {
        consumers.emplace_back([&queue, &received] {
            while (auto val = queue.pop()) {
                received.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::this_thread::sleep_for(10ms);
    queue.close();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(received.load(), 0);
}
