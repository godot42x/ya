#include "reflects-core/lib.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace
{

struct CallProbe
{
    int  value = 1;
    int *slot  = nullptr;

    void writeRef(int &target) { target = 7; }

    void observe(const int &target) { seen = &target; }

    void writePtr(int *pointer)
    {
        if (pointer) {
            *pointer = 9;
        }
    }

    int &member() { return value; }

    const int &memberConst() const { return value; }

    int *ptrMember() { return slot; }

    void takeNarrow(int8_t byte) { narrow = byte; }

    const int *seen   = nullptr;
    int8_t     narrow = 0;

    CallProbe() = default;
    explicit CallProbe(int v) : value(v) {}
};

struct CallProbeReflection
{
    CallProbeReflection()
    {
        Register<CallProbe>("CallProbe")
            .property("value", &CallProbe::value)
            .property("slot", &CallProbe::slot)
            .function("writeRef", &CallProbe::writeRef)
            .function("observe", &CallProbe::observe)
            .function("writePtr", &CallProbe::writePtr)
            .function("member", &CallProbe::member)
            .function("memberConst", &CallProbe::memberConst)
            .function("ptrMember", &CallProbe::ptrMember)
            .function("takeNarrow", &CallProbe::takeNarrow)
            .constructor()
            .constructor<int>();
    }
};

const CallProbeReflection kCallProbeReflection;

Class *probeClass()
{
    Class *cls = ClassRegistry::instance().getClass("CallProbe");
    EXPECT_NE(cls, nullptr);
    return cls;
}

} // namespace

TEST(InvokeAbi, ReferenceWriteLandsOnCallerObject)
{
    CallProbe probe;
    int       target = 1;
    probeClass()->invoke<void>("writeRef", &probe, target);
    EXPECT_EQ(target, 7);
}

TEST(InvokeAbi, OutputParameterRoundTripsThroughMetaValue)
{
    CallProbe        probe;
    refl::MetaValue  slot = refl::MetaValue::construct(refl::type_index_v<int>);
    refl::MetaValue *args = &slot;
    probeClass()->invoke("writeRef", &probe, std::span<refl::MetaValue>(args, 1));
    ASSERT_NE(slot.tryGet<int>(), nullptr);
    EXPECT_EQ(*slot.tryGet<int>(), 7);
}

TEST(InvokeAbi, ConstReferenceDoesNotCopy)
{
    CallProbe probe;
    int       original = 4;
    probeClass()->invoke<void>("observe", &probe, original);
    EXPECT_EQ(probe.seen, &original);

    refl::MetaValue boxed = refl::MetaValue::make(original);
    probeClass()->functions.at("observe").invoker(&probe, std::span<refl::MetaValue>(&boxed, 1));
    EXPECT_EQ(probe.seen, boxed.tryGet<int>());
    EXPECT_NE(probe.seen, &original);
}

TEST(InvokeAbi, PointerArgumentWritesThroughAndAcceptsNull)
{
    CallProbe probe;
    int       storage = 3;
    probeClass()->invoke<void>("writePtr", &probe, &storage);
    EXPECT_EQ(storage, 9);

    int *empty = nullptr;
    EXPECT_NO_THROW(probeClass()->invoke<void>("writePtr", &probe, empty));
    EXPECT_EQ(storage, 9);
}

TEST(InvokeAbi, ConstReferenceRejectsNonConstParameter)
{
    CallProbe  probe;
    const int  frozen = 1;
    EXPECT_THROW(probeClass()->invoke<void>("writeRef", &probe, frozen), std::runtime_error);
}

TEST(InvokeAbi, ReturnedReferenceAndPointerAliasTheObject)
{
    CallProbe probe;
    probe.value = 2;
    int &aliased = probeClass()->invoke<int &>("member", &probe);
    EXPECT_EQ(&aliased, &probe.value);
    aliased = 11;
    EXPECT_EQ(probe.value, 11);

    const int &readonly = probeClass()->invoke<const int &>("memberConst", &probe);
    EXPECT_EQ(&readonly, &probe.value);

    int  held = 5;
    probe.slot = &held;
    int *back = probeClass()->invoke<int *>("ptrMember", &probe);
    EXPECT_EQ(back, &held);
    *back = 6;
    EXPECT_EQ(held, 6);

    probe.slot = nullptr;
    refl::MetaValue result = probeClass()->functions.at("ptrMember").invoker(&probe, {});
    EXPECT_TRUE(result.isRef());
    EXPECT_EQ(result.data(), nullptr);
}

TEST(InvokeAbi, NumericConversionAndArity)
{
    CallProbe probe;
    probeClass()->invoke<void>("takeNarrow", &probe, int64_t{20});
    EXPECT_EQ(probe.narrow, 20);

    refl::MetaValue wide = refl::MetaValue::make(int64_t{300});
    EXPECT_THROW(probeClass()->functions.at("takeNarrow").invoker(&probe, std::span<refl::MetaValue>(&wide, 1)),
                 std::runtime_error);

    refl::MetaValue extra[] = {refl::MetaValue::make(int64_t{1}), refl::MetaValue::make(int64_t{2})};
    EXPECT_THROW(probeClass()->invoke("takeNarrow", &probe, extra), std::runtime_error);
}

TEST(InvokeAbi, ConstructorWithArguments)
{
    void *raw = probeClass()->createInstance(42);
    ASSERT_NE(raw, nullptr);
    auto *probe = static_cast<CallProbe *>(raw);
    EXPECT_EQ(probe->value, 42);
    probeClass()->destroyInstance(raw);
}

TEST(InvokeAbi, PointerMemberPointeeReadWriteAndNull)
{
    CallProbe probe;
    int       held = 8;
    probe.slot     = &held;
    Property &prop = probeClass()->properties.at("slot");

    refl::MetaValue through = prop.pointee(&probe);
    ASSERT_TRUE(through.isRef());
    ASSERT_NE(through.tryGet<int>(), nullptr);
    *through.tryGet<int>() = 15;
    EXPECT_EQ(held, 15);

    refl::MetaValue storage = prop.ref(&probe);
    EXPECT_EQ(storage.data(), static_cast<void *>(&probe.slot));

    probe.slot = nullptr;
    refl::MetaValue empty = prop.pointee(&probe);
    EXPECT_TRUE(empty.isRef());
    EXPECT_EQ(empty.data(), nullptr);
}
