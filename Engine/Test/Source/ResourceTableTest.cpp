#include "Resource/Handle/ResourceTable.h"

#include <gtest/gtest.h>
#include <memory>

using namespace ya;

namespace
{
struct Dummy
{
    int value = 0;
    explicit Dummy(int v) : value(v) {}
};
} // namespace

TEST(ResourceTableTest, AllocateAndGet)
{
    ResourceTable<Dummy> table;
    auto                 res = std::make_shared<Dummy>(42);
    auto                 h   = table.allocate(res);

    EXPECT_TRUE(h.isValid());
    EXPECT_TRUE(table.isValid(h));
    ASSERT_NE(table.get(h), nullptr);
    EXPECT_EQ(table.get(h)->value, 42);
    EXPECT_EQ(table.getShared(h), res);
}

TEST(ResourceTableTest, DefaultHandleIsInvalid)
{
    ResourceTable<Dummy>   table;
    FResourceHandle<Dummy> empty{};

    EXPECT_FALSE(empty.isValid());
    EXPECT_FALSE(table.isValid(empty));
    EXPECT_EQ(table.get(empty), nullptr);
    EXPECT_EQ(table.generationOf(empty), 0u);
}

TEST(ResourceTableTest, AllocateNullSlotThenReplace)
{
    ResourceTable<Dummy> table;
    auto                 h = table.allocate(); // lazy slot, no resource yet

    EXPECT_TRUE(table.isValid(h));
    EXPECT_EQ(table.get(h), nullptr); // valid slot but empty

    const uint32_t genBefore = table.generationOf(h);
    auto           old       = table.replace(h, std::make_shared<Dummy>(7));
    EXPECT_EQ(old, nullptr); // slot was empty

    // Same index, bumped generation -> old handle is now stale.
    EXPECT_FALSE(table.isValid(h));
    EXPECT_EQ(table.generationOf(h), genBefore + 1);
}

TEST(ResourceTableTest, ReleaseInvalidatesHandle)
{
    ResourceTable<Dummy> table;
    auto                 h   = table.allocate(std::make_shared<Dummy>(1));
    auto                 old = table.release(h);

    ASSERT_NE(old, nullptr);
    EXPECT_EQ(old->value, 1);
    EXPECT_FALSE(table.isValid(h));
    EXPECT_EQ(table.get(h), nullptr);
}

TEST(ResourceTableTest, RecycledSlotMakesOldHandleStale_ABA)
{
    ResourceTable<Dummy> table;
    auto                 h1 = table.allocate(std::make_shared<Dummy>(100));
    const uint32_t       idx = h1.index;

    table.release(h1);

    // New allocation reuses the same slot index but with a new generation.
    auto h2 = table.allocate(std::make_shared<Dummy>(200));
    EXPECT_EQ(h2.index, idx);                 // same slot reused
    EXPECT_NE(h2.generation, h1.generation);  // generation differs

    // Old handle must NOT resolve to the new resource (ABA guard).
    EXPECT_FALSE(table.isValid(h1));
    EXPECT_EQ(table.get(h1), nullptr);

    EXPECT_TRUE(table.isValid(h2));
    ASSERT_NE(table.get(h2), nullptr);
    EXPECT_EQ(table.get(h2)->value, 200);
}

TEST(ResourceTableTest, ReplaceReturnsOldResource)
{
    ResourceTable<Dummy> table;
    auto                 first = std::make_shared<Dummy>(1);
    auto                 h     = table.allocate(first);

    auto old = table.replace(h, std::make_shared<Dummy>(2));
    ASSERT_NE(old, nullptr);
    EXPECT_EQ(old->value, 1); // caller can retire this via DeferredDeletionQueue
}

TEST(ResourceTableTest, HandleHashAndEquality)
{
    FResourceHandle<Dummy> a{.index = 3, .generation = 5};
    FResourceHandle<Dummy> b{.index = 3, .generation = 5};
    FResourceHandle<Dummy> c{.index = 3, .generation = 6};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);

    std::hash<FResourceHandle<Dummy>> hasher;
    EXPECT_EQ(hasher(a), hasher(b));
}
