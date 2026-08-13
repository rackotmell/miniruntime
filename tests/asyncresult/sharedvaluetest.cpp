#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

#include "miniruntime/asyncresult/sharedvalue.h"

using namespace miniruntime::asyncresult;
using namespace std::chrono_literals;

TEST(SharedValueTest, SetWaitDeliversValue)
{
    SharedValue<int> value;
    value.set(7);

    auto result = value.wait();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 7);
}

TEST(SharedValueTest, WaitClearsSlot)
{
    SharedValue<int> value;
    value.set(1);
    ASSERT_TRUE(value.wait().has_value());

    // The slot is cleared after wait(); a fresh set is needed to unblock.
    std::atomic<bool> unblocked{false};
    std::thread waiter([&] {
        value.wait();
        unblocked.store(true);
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(unblocked.load());

    value.set(2);
    waiter.join();
    EXPECT_TRUE(unblocked.load());
}

TEST(SharedValueTest, KeepsLatestValue)
{
    SharedValue<int> value;
    value.set(1);
    value.set(2);
    value.set(3);

    auto result = value.wait();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 3);
}

TEST(SharedValueTest, PropagatesException)
{
    SharedValue<int> value;
    value.setException(std::make_exception_ptr(std::runtime_error("bad value")));

    EXPECT_THROW(value.wait(), std::runtime_error);
}

TEST(SharedValueTest, CloseReturnsNullopt)
{
    SharedValue<int> value;
    value.close();

    EXPECT_FALSE(value.wait().has_value());
}

TEST(SharedValueTest, CloseUnblocksWaiter)
{
    SharedValue<int> value;
    std::atomic<bool> unblocked{false};

    std::thread waiter([&] {
        auto result = value.wait();
        unblocked.store(!result.has_value());
    });

    std::this_thread::sleep_for(30ms);
    value.close();
    waiter.join();

    EXPECT_TRUE(unblocked.load());
}

TEST(SharedValueTest, WaitBlocksUntilNextSet)
{
    SharedValue<int> value;

    std::thread producer([&value] {
        std::this_thread::sleep_for(30ms);
        value.set(42);
    });

    auto result = value.wait();
    producer.join();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST(SharedValueTest, SetIdGetId)
{
    SharedValue<int> value;
    EXPECT_FALSE(value.getId().has_value());

    value.setId(99);
    ASSERT_TRUE(value.getId().has_value());
    EXPECT_EQ(value.getId().value(), 99);
}

TEST(SharedValueVoidTest, VoidSpecializationSetSignal)
{
    SharedValue<void> value;
    std::atomic<bool> signaled{false};

    std::thread thread([&]{
        value.wait();
        signaled.store(true);
    });
    value.set();
    thread.join();
    EXPECT_TRUE(signaled.load());
}

TEST(SharedValueVoidTest,  VoidSpecializationCloseUnblocksWaiter)
{
    SharedValue<void> value;
    value.close();

    EXPECT_FALSE(value.wait());
}

TEST(SharedValueVoidTest,  VoidSpecializationPropagatesException)
{
    SharedValue<void> value;
    value.setException(std::make_exception_ptr(std::runtime_error("bad tick")));

    EXPECT_THROW(value.wait(), std::runtime_error);
}

TEST(SharedValueTest, ConcurrentProducerConsumer)
{
    SharedValue<int> value;
    constexpr int count = 50;
    std::atomic<int> consumed{0};

    std::thread producer([&value, &consumed] {
        for (int i = 0; i < count; ++i) {
            value.set(i);
            // Wait until the consumer drains the value so none are overwritten.
            const auto deadline = std::chrono::steady_clock::now() + 1s;
            while (consumed.load() < i + 1 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(1ms);
            }
        }
        value.close();
    });

    std::thread consumer([&value, &consumed] {
        while (value.wait()) {
            consumed.fetch_add(1);
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(consumed.load(), count);
}
