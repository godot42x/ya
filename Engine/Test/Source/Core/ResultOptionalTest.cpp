#include "Core/Result.h"
#include "Core/Optional.h"

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

// ────────────────────────────────────────────────────────────────────────────
// Optional<T> tests
// ────────────────────────────────────────────────────────────────────────────

TEST(OptionalTest, SomeConstruction)
{
    Optional<int> opt{42};
    
    EXPECT_TRUE(opt.isValid());
    EXPECT_TRUE(opt.isSome());
    EXPECT_FALSE(opt.isNone());
    EXPECT_TRUE(static_cast<bool>(opt));
    EXPECT_EQ(opt.value(), 42);
    EXPECT_EQ(*opt, 42);
}

TEST(OptionalTest, NoneConstruction)
{
    Optional<int> opt;
    
    EXPECT_FALSE(opt.isValid());
    EXPECT_FALSE(opt.isSome());
    EXPECT_TRUE(opt.isNone());
    EXPECT_FALSE(static_cast<bool>(opt));
}

TEST(OptionalTest, ValueOrReturnsSome)
{
    Optional<int> opt{42};
    EXPECT_EQ(opt.valueOr(0), 42);
}

TEST(OptionalTest, ValueOrReturnsDefault)
{
    Optional<int> opt;
    EXPECT_EQ(opt.valueOr(99), 99);
}

TEST(OptionalTest, ResetMakesNone)
{
    Optional<int> opt{42};
    EXPECT_TRUE(opt.isValid());
    
    opt.reset();
    EXPECT_FALSE(opt.isValid());
}

TEST(OptionalTest, SomeHelperFunction)
{
    auto opt = Some(42);
    EXPECT_TRUE(opt.isValid());
    EXPECT_EQ(opt.value(), 42);
}

TEST(OptionalTest, NoneHelperFunction)
{
    auto opt = None<int>();
    EXPECT_FALSE(opt.isValid());
}

TEST(OptionalTest, Equality)
{
    Optional<int> a{42};
    Optional<int> b{42};
    Optional<int> c{99};
    Optional<int> none1;
    Optional<int> none2;
    
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, none1);
    EXPECT_EQ(none1, none2);
}

// ────────────────────────────────────────────────────────────────────────────
// OptionalHandle<T> tests
// ────────────────────────────────────────────────────────────────────────────

struct TextureHandle {};
struct BufferHandle {};

TEST(OptionalHandleTest, ValidConstruction)
{
    OptionalHandle<TextureHandle> handle{5};
    
    EXPECT_TRUE(handle.isValid());
    EXPECT_TRUE(handle.isSome());
    EXPECT_FALSE(handle.isNone());
    EXPECT_TRUE(static_cast<bool>(handle));
    EXPECT_EQ(handle.index(), 5u);
}

TEST(OptionalHandleTest, InvalidConstruction)
{
    OptionalHandle<TextureHandle> handle;
    
    EXPECT_FALSE(handle.isValid());
    EXPECT_FALSE(handle.isSome());
    EXPECT_TRUE(handle.isNone());
    EXPECT_FALSE(static_cast<bool>(handle));
    EXPECT_EQ(handle.index(), ~0u);
}

TEST(OptionalHandleTest, TypeSafety)
{
    OptionalHandle<TextureHandle> texture{0};
    OptionalHandle<BufferHandle> buffer{0};
    
    // Same index but different types
    EXPECT_TRUE(texture.isValid());
    EXPECT_TRUE(buffer.isValid());
    EXPECT_EQ(texture.index(), 0u);
    EXPECT_EQ(buffer.index(), 0u);
    
    // Cannot cross-assign different handle types
    // This would fail to compile:
    // buffer = texture;
}

TEST(OptionalHandleTest, ValueOrReturnsValid)
{
    OptionalHandle<TextureHandle> handle{7};
    EXPECT_EQ(handle.valueOr(0), 7u);
}

TEST(OptionalHandleTest, ValueOrReturnsDefault)
{
    OptionalHandle<TextureHandle> handle;
    EXPECT_EQ(handle.valueOr(99), 99u);
}

TEST(OptionalHandleTest, ResetMakesInvalid)
{
    OptionalHandle<TextureHandle> handle{42};
    EXPECT_TRUE(handle.isValid());
    
    handle.reset();
    EXPECT_FALSE(handle.isValid());
    EXPECT_EQ(handle.index(), ~0u);
}

TEST(OptionalHandleTest, ZeroSizeOverhead)
{
    // Tag type should be zero-size
    static_assert(std::is_empty_v<TextureHandle>);
    static_assert(std::is_empty_v<BufferHandle>);
    
    // OptionalHandle should be just uint32_t
    static_assert(sizeof(OptionalHandle<TextureHandle>) == sizeof(uint32_t));
    static_assert(sizeof(OptionalHandle<BufferHandle>) == sizeof(uint32_t));
}

} // namespace ya
