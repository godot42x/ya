#include "Core/Result.h"

#include <gtest/gtest.h>
#include <string>

namespace ya
{

// ────────────────────────────────────────────────────────────────────────────
// Result<T, E> tests
// ────────────────────────────────────────────────────────────────────────────

enum class TestError
{
    NotFound,
    InvalidInput,
    Timeout,
};

TEST(ResultTest, OkConstruction)
{
    Result<int, TestError> result = 42;
    
    EXPECT_TRUE(result.isOk());
    EXPECT_FALSE(result.isErr());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), 42);
    EXPECT_EQ(*result, 42);
}

TEST(ResultTest, ErrConstruction)
{
    Result<int, TestError> result = TestError::NotFound;
    
    EXPECT_FALSE(result.isOk());
    EXPECT_TRUE(result.isErr());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error(), TestError::NotFound);
}

TEST(ResultTest, MapTransformsOk)
{
    Result<int, TestError> result = 10;
    
    auto doubled = result.map([](int x) { return x * 2; });
    
    EXPECT_TRUE(doubled.isOk());
    EXPECT_EQ(doubled.value(), 20);
}

TEST(ResultTest, MapPreservesErr)
{
    Result<int, TestError> result = TestError::InvalidInput;
    
    auto doubled = result.map([](int x) { return x * 2; });
    
    EXPECT_TRUE(doubled.isErr());
    EXPECT_EQ(doubled.error(), TestError::InvalidInput);
}

TEST(ResultTest, AndThenChainsOk)
{
    auto divide = [](int x) -> Result<int, TestError> {
        if (x == 0) return TestError::InvalidInput;
        return 100 / x;
    };
    
    Result<int, TestError> result = 5;
    auto chained = result.andThen(divide);
    
    EXPECT_TRUE(chained.isOk());
    EXPECT_EQ(chained.value(), 20);
}

TEST(ResultTest, AndThenShortCircuitsOnErr)
{
    auto divide = [](int x) -> Result<int, TestError> {
        if (x == 0) return TestError::InvalidInput;
        return 100 / x;
    };
    
    Result<int, TestError> result = TestError::NotFound;
    auto chained = result.andThen(divide);
    
    EXPECT_TRUE(chained.isErr());
    EXPECT_EQ(chained.error(), TestError::NotFound);
}

TEST(ResultTest, ValueOrReturnsValue)
{
    Result<int, TestError> result = 42;
    EXPECT_EQ(result.valueOr(0), 42);
}

TEST(ResultTest, ValueOrReturnsDefault)
{
    Result<int, TestError> result = TestError::Timeout;
    EXPECT_EQ(result.valueOr(99), 99);
}

TEST(ResultTest, MoveSemantics)
{
    Result<std::string, TestError> result = std::string("hello");
    
    EXPECT_TRUE(result.isOk());
    std::string moved = std::move(result).value();
    EXPECT_EQ(moved, "hello");
}

// Result<void, E>: the value-less case, used by composition steps that report
// only whether the call happened.
TEST(ResultTest, VoidResultReportsSuccessWithoutAValue)
{
    const Result<void, TestError> ok = Result<void, TestError>::success();
    EXPECT_TRUE(ok.isOk());
    EXPECT_FALSE(ok.isErr());
    EXPECT_TRUE(static_cast<bool>(ok));

    const Result<void, TestError> failed = TestError::Timeout;
    EXPECT_TRUE(failed.isErr());
    EXPECT_EQ(failed.error(), TestError::Timeout);
}

// ────────────────────────────────────────────────────────────────────────────
} // namespace ya
