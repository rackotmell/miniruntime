#include <gtest/gtest.h>
#include <thread>

#include "miniruntime/task/boundedblockingqueue.h"

using namespace miniruntime::task;
using namespace std::chrono_literals;

TEST(BoundedBlockingQueueTest, PushPop)
{
    BoundedBlockingQueue<int> queue(2);

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

TEST(BoundedBlockingQueueTest, TimeoutPopValue)
{
    BoundedBlockingQueue<int> queue(1);
    ASSERT_TRUE(queue.push(1));

    auto val = queue.timeoutPop(30ms);
    EXPECT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 1);
}

TEST(BoundedBlockingQueueTest, TimeoutPopTimeout)
{
    BoundedBlockingQueue<int> queue(1);

    auto val = queue.timeoutPop(30ms);
    EXPECT_FALSE(val.has_value());
}

TEST(BoundedBlockingQueueTest, Emplace)
{
    BoundedBlockingQueue<std::string> queue(1);

    ASSERT_TRUE(queue.push("Hello"));
    EXPECT_EQ(queue.size(), 1);

    auto val = queue.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "Hello");
}

TEST(BoundedBlockingQueueTest, CloseUnblocksPopAndRejectsPush)
{
    BoundedBlockingQueue<int> queue(1);
    queue.close();

    EXPECT_FALSE(queue.pop().has_value());
    EXPECT_FALSE(queue.push(1));
}

TEST(BoundedBlockingQueueTest, ConcurrentPushPop)
{
    BoundedBlockingQueue<int> queue(8);
    constexpr int count = 50000;
    int received{0};

    std::thread consumer([&] {
        while (auto value = queue.pop()) {
            ++received;
        }
    });

    std::thread producer([&] {
        for (int i = 0; i < count; ++i)
            ASSERT_TRUE(queue.push(i));
        queue.close();
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received, count);
}