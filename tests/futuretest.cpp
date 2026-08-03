#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

#include "future.h"

using namespace miniruntime;

TEST(FutureTest, DeliverValue)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    EXPECT_FALSE(future.isReady());
    promise.setValue(1);
    EXPECT_TRUE(future.isReady());
    auto result = future.get();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1);
}

TEST(FutureTest, PropagatesException)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    promise.setException(std::make_exception_ptr(std::runtime_error("boom")));
    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(FutureTest, CloseReturnsNullopt)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    promise.close();
    EXPECT_FALSE(future.get().has_value());
}

TEST(FutureTest, DuplicateSetIsIgnored)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    promise.setValue(1);
    promise.setValue(2);
    EXPECT_EQ(future.get().value(), 1);
}

TEST(FutureTest, ThreadBlockUnblock)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    int recieved;
    std::thread thread{[&] {
        auto val = future.get();
        ASSERT_TRUE(val.has_value());
        recieved = val.value();
    }};

    promise.setValue(1);

    thread.join();
    ASSERT_EQ(recieved, 1);
}

TEST(FutureTest, SetIdGetId)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    EXPECT_FALSE(future.getId().has_value());

    promise.setId(77);
    ASSERT_TRUE(future.getId().has_value());
    EXPECT_EQ(future.getId().value(), 77);
}

TEST(FutureTest, StatesTransitions)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    EXPECT_FALSE(future.isReady());
    EXPECT_FALSE(future.isClosed());

    promise.setValue(1);
    EXPECT_TRUE(future.isReady());
    EXPECT_FALSE(future.isClosed());
}

TEST(FutureTest, CloseSetsClosedState)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    promise.close();
    EXPECT_TRUE(future.isClosed());
    EXPECT_FALSE(future.isReady());
}

TEST(FutureTest, FuturesAreMovable)
{
    Promise<int> promise;
    auto future = promise.getFuture();
    auto moved = std::move(future);

    promise.setValue(5);

    ASSERT_TRUE(moved.get().has_value());
    EXPECT_EQ(moved.get().value(), 5);
}

TEST(FutureTest, ExceptionUnblocksWaiter)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    std::thread thread{[&future] {
        EXPECT_THROW(future.get(), std::runtime_error);
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    promise.setException(std::make_exception_ptr(std::runtime_error("late boom")));

    thread.join();
}

TEST(FutureTest, CloseUnblocksWaiter)
{
    Promise<int> promise;
    auto future = promise.getFuture();

    std::atomic<bool> returned{false};
    std::thread thread{[&future, &returned] {
        EXPECT_FALSE(future.get().has_value());
        returned.store(true);
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    promise.close();

    thread.join();
    EXPECT_TRUE(returned.load());
}

TEST(FutureTest, VoidSpecializationGet)
{
    Promise<void> promise;
    auto future = promise.getFuture();

    promise.setValue();
    EXPECT_NO_THROW(future.get());
}

TEST(FutureTest, VoidSpecializationPropagatesException)
{
    Promise<void> promise;
    auto future = promise.getFuture();

    promise.setException(std::make_exception_ptr(std::runtime_error("void boom")));
    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(FutureTest, VoidSpecializationClose)
{
    Promise<void> promise;
    auto future = promise.getFuture();

    promise.close();
    EXPECT_NO_THROW(future.get());
}