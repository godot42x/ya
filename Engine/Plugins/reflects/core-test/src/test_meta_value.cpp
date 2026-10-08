#include "reflects-core/lib.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <string>

namespace
{

struct Counted
{
    static int alive;
    static int constructed;
    static int destroyed;

    int tag = 0;

    Counted()
    {
        ++alive;
        ++constructed;
    }
    Counted(const Counted &other) : tag(other.tag)
    {
        ++alive;
        ++constructed;
    }
    Counted(Counted &&other) noexcept : tag(other.tag)
    {
        ++alive;
        ++constructed;
    }
    ~Counted()
    {
        --alive;
        ++destroyed;
    }

    Counted &operator=(const Counted &) = default;
    Counted &operator=(Counted &&)      = default;

    static void reset()
    {
        alive       = 0;
        constructed = 0;
        destroyed   = 0;
    }
};

int Counted::alive       = 0;
int Counted::constructed = 0;
int Counted::destroyed   = 0;

struct Fit
{
    char bytes[32] = {};
};
struct Overflow
{
    char bytes[33] = {};
};
struct alignas(32) WideAlign
{
    char byte = 0;
};

struct NoDefault
{
    explicit NoDefault(int) {}
};

struct MoveOnly
{
    MoveOnly()                   = default;
    MoveOnly(const MoveOnly &)   = delete;
    MoveOnly(MoveOnly &&)        = default;
};

struct UnregisteredRef
{
    int value = 1;
};

void copyAssign(refl::MetaValue &dst, const refl::MetaValue &src)
{
    dst = src;
}

void moveAssign(refl::MetaValue &dst, refl::MetaValue &src)
{
    dst = std::move(src);
}

void selfAssign(refl::MetaValue &value)
{
    copyAssign(value, value);
}

void selfMove(refl::MetaValue &value)
{
    moveAssign(value, value);
}

} // namespace

TEST(MetaValue, Layout)
{
    std::cout << "sizeof(MetaValue)=" << sizeof(refl::MetaValue) << " alignof=" << alignof(refl::MetaValue) << "\n";
    EXPECT_LE(sizeof(refl::MetaValue), 64u);
    EXPECT_GE(alignof(refl::MetaValue), 16u);
}

TEST(MetaValue, InlineCopyMoveAndStorageChoice)
{
    refl::MetaValue value = refl::MetaValue::make(41);
    EXPECT_FALSE(value.isNull());
    EXPECT_FALSE(value.isRef());
    EXPECT_EQ(value.storage(), refl::EMetaStorage::Inline);
    EXPECT_EQ(value.type(), refl::type_index_v<int>);
    ASSERT_NE(value.tryGet<int>(), nullptr);
    EXPECT_EQ(*value.tryGet<int>(), 41);

    refl::MetaValue copy = value;
    *value.tryGet<int>() = 5;
    EXPECT_EQ(*copy.tryGet<int>(), 41);
    EXPECT_EQ(*value.tryGet<int>(), 5);

    refl::MetaValue moved = std::move(value);
    EXPECT_TRUE(value.isNull());
    EXPECT_EQ(*moved.tryGet<int>(), 5);
    selfAssign(moved);
    selfMove(moved);
    EXPECT_EQ(*moved.tryGet<int>(), 5);

    Fit fit{};
    fit.bytes[0] = 9;
    EXPECT_EQ(refl::MetaValue::make(fit).storage(), refl::EMetaStorage::Inline);
    EXPECT_EQ(refl::MetaValue::make(Overflow{}).storage(), refl::EMetaStorage::Boxed);
    EXPECT_EQ(refl::MetaValue::make(WideAlign{}).storage(), refl::EMetaStorage::Boxed);
    EXPECT_EQ(refl::MetaValue::make(std::string("x")).storage(), refl::EMetaStorage::Boxed);
}

TEST(MetaValue, BoxedLifetimeCopyMoveAndSelfAssign)
{
    Counted::reset();
    refl::TypeRegistry::instance().registerType<Counted>("RR1Counted");
    {
        refl::MetaValue value = refl::MetaValue::make(Counted{});
        EXPECT_EQ(value.storage(), refl::EMetaStorage::Boxed);
        EXPECT_EQ(Counted::alive, 1);
        value.tryGet<Counted>()->tag = 5;

        refl::MetaValue copy = value;
        EXPECT_EQ(Counted::alive, 2);
        EXPECT_EQ(copy.tryGet<Counted>()->tag, 5);
        copy.tryGet<Counted>()->tag = 8;
        EXPECT_EQ(value.tryGet<Counted>()->tag, 5);

        refl::MetaValue moved = std::move(copy);
        EXPECT_TRUE(copy.isNull());
        EXPECT_EQ(Counted::alive, 2);
        EXPECT_EQ(moved.tryGet<Counted>()->tag, 8);

        selfAssign(value);
        selfMove(value);
        EXPECT_FALSE(value.isNull());
        EXPECT_EQ(value.tryGet<Counted>()->tag, 5);

        refl::MetaValue other = refl::MetaValue::make(Counted{});
        other.tryGet<Counted>()->tag = 2;
        value                         = std::move(other);
        EXPECT_TRUE(other.isNull());
        EXPECT_EQ(value.tryGet<Counted>()->tag, 2);

        value = refl::MetaValue{};
        EXPECT_TRUE(value.isNull());
    }
    EXPECT_EQ(Counted::alive, 0);
    EXPECT_EQ(Counted::constructed, Counted::destroyed);
}

TEST(MetaValue, RefWriteConstRefAndMaterialize)
{
    int number = 3;
    refl::MetaValue ref = refl::MetaValue::ref(number);
    EXPECT_TRUE(ref.isRef());
    EXPECT_EQ(ref.storage(), refl::EMetaStorage::Ref);
    ASSERT_NE(ref.tryGet<int>(), nullptr);
    *ref.tryGet<int>() = 9;
    EXPECT_EQ(number, 9);

    refl::MetaValue refCopy = ref;
    *refCopy.tryGet<int>()  = 4;
    EXPECT_EQ(number, 4);

    const int constant = 6;
    refl::MetaValue cref = refl::MetaValue::cref(constant);
    EXPECT_EQ(cref.tryGet<int>(), nullptr);
    ASSERT_NE(std::as_const(cref).tryGet<int>(), nullptr);
    EXPECT_EQ(*std::as_const(cref).tryGet<int>(), 6);

    const int viaConstDeduction = 8;
    refl::MetaValue deduced     = refl::MetaValue::ref(viaConstDeduction);
    EXPECT_EQ(deduced.tryGet<int>(), nullptr);
    EXPECT_EQ(*std::as_const(deduced).tryGet<int>(), 8);

    refl::MetaValue dynamic = refl::MetaValue::ref(refl::type_index_v<int>, &number, false);
    *dynamic.tryGet<int>()  = 12;
    EXPECT_EQ(number, 12);
    refl::MetaValue dynamicConst = refl::MetaValue::ref(refl::type_index_v<int>, &number, true);
    EXPECT_EQ(dynamicConst.tryGet<int>(), nullptr);
    EXPECT_EQ(*std::as_const(dynamicConst).tryGet<int>(), 12);

    refl::MetaValue owned = ref.materialize();
    EXPECT_FALSE(owned.isRef());
    EXPECT_EQ(owned.storage(), refl::EMetaStorage::Inline);
    EXPECT_EQ(*owned.tryGet<int>(), 12);
    number = 1;
    EXPECT_EQ(*owned.tryGet<int>(), 12);

    refl::MetaValue inlineOwned = refl::MetaValue::make(15);
    refl::MetaValue materialized = inlineOwned.materialize();
    *inlineOwned.tryGet<int>()   = 16;
    EXPECT_EQ(*materialized.tryGet<int>(), 15);

    Counted::reset();
    refl::TypeRegistry::instance().registerType<Counted>("RR1Counted");
    {
        Counted counted;
        counted.tag = 9;
        const int aliveBefore = Counted::alive;
        refl::MetaValue countedRef = refl::MetaValue::ref(counted);
        EXPECT_EQ(Counted::alive, aliveBefore);
        countedRef.tryGet<Counted>()->tag = 10;
        EXPECT_EQ(counted.tag, 10);

        refl::MetaValue boxed = countedRef.materialize();
        EXPECT_EQ(boxed.storage(), refl::EMetaStorage::Boxed);
        EXPECT_EQ(boxed.tryGet<Counted>()->tag, 10);
        counted.tag = 1;
        EXPECT_EQ(boxed.tryGet<Counted>()->tag, 10);
    }
    EXPECT_EQ(Counted::alive, 0);
    EXPECT_EQ(Counted::constructed, Counted::destroyed);

    UnregisteredRef plain;
    refl::MetaValue missing = refl::MetaValue::ref(plain);
    EXPECT_THROW(missing.materialize(), std::runtime_error);

    refl::TypeRegistry::instance().registerType<MoveOnly>("RR1MoveOnly");
    MoveOnly moveOnly;
    refl::MetaValue moveRef = refl::MetaValue::ref(moveOnly);
    EXPECT_THROW(moveRef.materialize(), std::runtime_error);
}

TEST(MetaValue, ConstructByType)
{
    refl::MetaValue integer = refl::MetaValue::construct(refl::type_index_v<int>);
    EXPECT_EQ(integer.storage(), refl::EMetaStorage::Inline);
    ASSERT_NE(integer.tryGet<int>(), nullptr);
    EXPECT_EQ(*integer.tryGet<int>(), 0);

    const refl::TypeOps *ops = refl::TypeRegistry::instance().find(refl::type_index_v<int>);
    ASSERT_NE(ops, nullptr);
    refl::MetaValue fromOps = refl::MetaValue::construct(*ops);
    EXPECT_EQ(*fromOps.tryGet<int>(), 0);

    refl::MetaValue text = refl::MetaValue::construct(refl::type_index_v<std::string>);
    EXPECT_EQ(text.storage(), refl::EMetaStorage::Boxed);
    ASSERT_NE(text.tryGet<std::string>(), nullptr);
    EXPECT_TRUE(text.tryGet<std::string>()->empty());
    ASSERT_NE(text.asString(), nullptr);
    EXPECT_EQ(*text.asString(), "");

    struct Missing
    {
    };
    EXPECT_THROW(refl::MetaValue::construct(refl::type_index_v<Missing>), std::runtime_error);

    refl::TypeRegistry::instance().registerType<NoDefault>("RR1NoDefault");
    EXPECT_THROW(refl::MetaValue::construct(refl::type_index_v<NoDefault>), std::runtime_error);
}

TEST(MetaValue, ConversionMatrix)
{
    refl::MetaValue integer = refl::MetaValue::make(300);
    EXPECT_EQ(integer.toInt64(), 300);
    EXPECT_EQ(integer.toUInt64(), 300u);
    EXPECT_DOUBLE_EQ(*integer.toDouble(), 300.0);
    EXPECT_FALSE(integer.toBool().has_value());
    EXPECT_EQ(integer.asString(), nullptr);
    EXPECT_FALSE(integer.convert<bool>().has_value());
    EXPECT_FALSE(integer.convert<int8_t>().has_value());
    EXPECT_FALSE(integer.convert<uint8_t>().has_value());
    ASSERT_TRUE(integer.convert<int16_t>().has_value());
    EXPECT_EQ(*integer.convert<int16_t>(), 300);
    EXPECT_EQ(*integer.convert<double>(), 300.0);

    int narrowed = 0;
    EXPECT_TRUE(integer.convertTo(refl::type_index_v<int>, &narrowed));
    EXPECT_EQ(narrowed, 300);
    int8_t tooNarrow = 0;
    EXPECT_FALSE(integer.convertTo(refl::type_index_v<int8_t>, &tooNarrow));

    refl::MetaValue negative = refl::MetaValue::make(-1);
    EXPECT_EQ(negative.toInt64(), -1);
    EXPECT_FALSE(negative.toUInt64().has_value());
    EXPECT_FALSE(negative.convert<unsigned int>().has_value());
    EXPECT_FALSE(negative.convert<uint8_t>().has_value());
    EXPECT_EQ(*negative.convert<int>(), -1);

    refl::MetaValue huge = refl::MetaValue::make(std::numeric_limits<unsigned long long>::max());
    EXPECT_FALSE(huge.toInt64().has_value());
    EXPECT_EQ(*huge.toUInt64(), std::numeric_limits<unsigned long long>::max());
    EXPECT_TRUE(huge.toDouble().has_value());
    EXPECT_FALSE(huge.convert<int64_t>().has_value());

    const auto unsignedLongMax = std::numeric_limits<unsigned long>::max();
    refl::MetaValue ulongValue = refl::MetaValue::make(unsignedLongMax);
    if (static_cast<uint64_t>(unsignedLongMax) > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        EXPECT_FALSE(ulongValue.toInt64().has_value());
    }
    else {
        EXPECT_EQ(*ulongValue.toInt64(), static_cast<int64_t>(unsignedLongMax));
    }
    EXPECT_EQ(refl::TypeRegistry::instance().find(refl::type_index_v<long>)->life.size, sizeof(long));

    refl::MetaValue longValue = refl::MetaValue::make(long{-2});
    EXPECT_EQ(*longValue.toInt64(), -2);
    EXPECT_FALSE(longValue.toUInt64().has_value());
    if (sizeof(long) > sizeof(int)) {
        refl::MetaValue wide = refl::MetaValue::make(std::numeric_limits<long>::max());
        EXPECT_FALSE(wide.convert<int>().has_value());
    }
    else {
        EXPECT_TRUE(refl::MetaValue::make(std::numeric_limits<long>::max()).convert<int>().has_value());
    }

    refl::MetaValue fractional = refl::MetaValue::make(1.5f);
    EXPECT_FALSE(fractional.toInt64().has_value());
    EXPECT_FALSE(fractional.convert<int>().has_value());
    EXPECT_FALSE(fractional.toBool().has_value());
    EXPECT_DOUBLE_EQ(*fractional.toDouble(), 1.5);

    refl::MetaValue whole = refl::MetaValue::make(4.0);
    EXPECT_EQ(*whole.toInt64(), 4);
    EXPECT_EQ(*whole.convert<int>(), 4);
    EXPECT_FALSE(whole.convert<bool>().has_value());

    refl::MetaValue tooBig = refl::MetaValue::make(1.0e20);
    EXPECT_FALSE(tooBig.toInt64().has_value());
    EXPECT_FALSE(tooBig.convert<int64_t>().has_value());

    refl::MetaValue int64Min = refl::MetaValue::make(-9223372036854775808.0);
    ASSERT_TRUE(int64Min.toInt64().has_value());
    EXPECT_EQ(*int64Min.toInt64(), std::numeric_limits<int64_t>::min());
    EXPECT_FALSE(int64Min.toUInt64().has_value());

    refl::MetaValue int64Limit = refl::MetaValue::make(9223372036854775808.0);
    EXPECT_FALSE(int64Limit.toInt64().has_value());
    ASSERT_TRUE(int64Limit.toUInt64().has_value());
    EXPECT_EQ(*int64Limit.toUInt64(), 9223372036854775808ull);

    refl::MetaValue flag = refl::MetaValue::make(true);
    EXPECT_FALSE(flag.toInt64().has_value());
    EXPECT_FALSE(flag.toUInt64().has_value());
    EXPECT_FALSE(flag.toDouble().has_value());
    EXPECT_FALSE(flag.convert<int>().has_value());
    ASSERT_TRUE(flag.toBool().has_value());
    EXPECT_TRUE(*flag.toBool());
    EXPECT_EQ(*flag.convert<bool>(), true);
    EXPECT_FALSE(refl::MetaValue::make(1).convert<bool>().has_value());
    EXPECT_FALSE(refl::MetaValue::make(0).toBool().has_value());

    refl::MetaValue text = refl::MetaValue::make(std::string("hi"));
    ASSERT_NE(text.asString(), nullptr);
    EXPECT_EQ(*text.asString(), "hi");
    EXPECT_FALSE(text.toInt64().has_value());
    EXPECT_FALSE(text.convert<int>().has_value());
    EXPECT_EQ(*text.convert<std::string>(), "hi");
    std::string occupied = "keep";
    EXPECT_FALSE(text.convertTo(refl::type_index_v<std::string>, &occupied));
    EXPECT_EQ(occupied, "keep");

    refl::MetaValue negZero = refl::MetaValue::make(-0.0);
    EXPECT_EQ(*negZero.toInt64(), 0);
    ASSERT_TRUE(negZero.toUInt64().has_value());
    EXPECT_EQ(*negZero.toUInt64(), 0u);
    ASSERT_TRUE(negZero.convert<unsigned int>().has_value());
    EXPECT_EQ(*negZero.convert<unsigned int>(), 0u);
    ASSERT_TRUE(negZero.convert<uint64_t>().has_value());
    EXPECT_EQ(*negZero.convert<uint64_t>(), 0u);
    unsigned stored = 7;
    EXPECT_TRUE(negZero.convertTo(refl::type_index_v<unsigned>, &stored));
    EXPECT_EQ(stored, 0u);

    refl::MetaValue negZeroF = refl::MetaValue::make(-0.0f);
    ASSERT_TRUE(negZeroF.toUInt64().has_value());
    EXPECT_EQ(*negZeroF.toUInt64(), 0u);
    ASSERT_TRUE(negZeroF.convert<unsigned>().has_value());
    EXPECT_EQ(*negZeroF.convert<unsigned>(), 0u);

    std::string live = "yo";
    refl::MetaValue textRef = refl::MetaValue::ref(live);
    EXPECT_EQ(textRef.asString(), &live);

    int liveInt = 12;
    refl::MetaValue liveRef = refl::MetaValue::ref(liveInt);
    EXPECT_EQ(*liveRef.toInt64(), 12);
    liveInt = 400;
    EXPECT_EQ(*liveRef.convert<int16_t>(), 400);

    refl::MetaValue nan = refl::MetaValue::make(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(nan.toInt64().has_value());
    EXPECT_FALSE(nan.convert<int>().has_value());
    EXPECT_FALSE(nan.convert<float>().has_value());
}
