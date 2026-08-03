#include <gtest/gtest.h>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "boundedblockingqueue.h"
#include "future.h"
#include "sharedvalue.h"

//================================================
// Auxiliary types
//================================================

struct Point {
    int x;
    int y;
    friend bool operator==(const Point& first, const Point& second) = default;
};
std::ostream& operator<<(std::ostream& ostream, const Point& point)
{
    return ostream << "Point(" << point.x << ", " << point.y << ")";
}

class MoveOnly
{
public:
    explicit MoveOnly(int v) : m_value(v) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(MoveOnly&&) = default;
    int value() const { return m_value; }

private:
    int m_value;
};
bool operator==(const MoveOnly& a, const MoveOnly& b) { return a.value() == b.value(); }
std::ostream& operator<<(std::ostream& os, const MoveOnly& m)
{
    return os << "MoveOnly{" << m.value() << "}";
}

struct NoDefault {
    explicit NoDefault(int v) : value(v) {}
    NoDefault() = delete;
    int value;
};
bool operator==(const NoDefault& a, const NoDefault& b) { return a.value == b.value; }
std::ostream& operator<<(std::ostream& os, const NoDefault& n)
{
    return os << "NoDefault{" << n.value << "}";
}

template <typename T> T makeValue()
{
    if constexpr (std::is_same_v<T, int>) {
        return 1;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return std::string("str");
    } else if constexpr (std::is_same_v<T, Point>) {
        return Point{1, 2};
    } else if constexpr (std::is_same_v<T, MoveOnly>) {
        return MoveOnly(2);
    } else if constexpr (std::is_same_v<T, NoDefault>) {
        return NoDefault(3);
    }
}

// ================================================
// Queue type tests
// ================================================

using namespace miniruntime;

template <typename T> class QueueTypeTest : public testing::Test
{
};

using QueueValueTypes = testing::Types<int, std::string, Point, MoveOnly, NoDefault>;

TYPED_TEST_SUITE(QueueTypeTest, QueueValueTypes);

TYPED_TEST(QueueTypeTest, PushPopAcrossTypes)
{
    BoundedBlockingQueue<TypeParam> queue(1);

    ASSERT_TRUE(queue.push(makeValue<TypeParam>()));
    EXPECT_EQ(queue.size(), 1);

    auto val = queue.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), makeValue<TypeParam>());
}

TYPED_TEST(QueueTypeTest, CloseAcrossTypes)
{
    BoundedBlockingQueue<TypeParam> queue(4);

    queue.push(makeValue<TypeParam>());
    ASSERT_EQ(queue.size(), 1);

    queue.close();

    ASSERT_TRUE(queue.pop().has_value());
    EXPECT_FALSE(queue.pop().has_value());
}

TYPED_TEST(QueueTypeTest, TimeoutPopAcrossTypes)
{
    BoundedBlockingQueue<TypeParam> queue(1);

    queue.push(makeValue<TypeParam>());
    auto val = queue.timeoutPop(std::chrono::milliseconds(30));
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), makeValue<TypeParam>());

    EXPECT_FALSE(queue.timeoutPop(std::chrono::milliseconds(10)).has_value());
}

// ================================================
// Future type tests
// ================================================

template <typename T> class FutureTypeTest : public testing::Test
{
};

// MoveOnly is intentionally excluded: Future/SharedValue deliver results
// through std::optional by copy (see future.h get(), sharedvalue.h wait()).
using FutureValueTypes = testing::Types<int, std::string, Point, NoDefault>;

TYPED_TEST_SUITE(FutureTypeTest, FutureValueTypes);

TYPED_TEST(FutureTypeTest, DeliversValueAcrossTypes)
{
    Promise<TypeParam> promise;
    auto future = promise.getFuture();

    promise.setValue(makeValue<TypeParam>());
    auto val = future.get();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), makeValue<TypeParam>());
}

TYPED_TEST(FutureTypeTest, ExceptionAcrossTypes)
{
    Promise<TypeParam> promise;
    auto future = promise.getFuture();

    promise.setException(std::make_exception_ptr(std::runtime_error("boom")));
    EXPECT_THROW(future.get(), std::runtime_error);
}

TYPED_TEST(FutureTypeTest, CloseAcrossTypes)
{
    Promise<TypeParam> promise;
    auto future = promise.getFuture();

    promise.close();
    EXPECT_FALSE(future.get().has_value());
}

// ================================================
// SharedValue type tests
// ================================================

template <typename T> class SharedValueTypeTest : public testing::Test
{
};

TYPED_TEST_SUITE(SharedValueTypeTest, FutureValueTypes);

TYPED_TEST(SharedValueTypeTest, SetWaitAcrossTypes)
{
    SharedValue<TypeParam> value;

    value.set(makeValue<TypeParam>());
    auto val = value.wait();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), makeValue<TypeParam>());
}

TYPED_TEST(SharedValueTypeTest, ExceptionAcrossTypes)
{
    SharedValue<TypeParam> value;

    value.setException(std::make_exception_ptr(std::runtime_error("boom")));
    EXPECT_THROW(value.wait(), std::runtime_error);
}

TYPED_TEST(SharedValueTypeTest, CloseAcrossTypes)
{
    SharedValue<TypeParam> value;

    value.close();
    EXPECT_FALSE(value.wait().has_value());
}