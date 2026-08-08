#include "Resource/Handle/PathRegistry.h"
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

TEST(PathRegistryTest, FindMissingReturnsInvalid)
{
    PathRegistry<Dummy> registry;
    EXPECT_FALSE(registry.find("missing.png").isValid());
}

TEST(PathRegistryTest, BindThenFindRoundTrips)
{
    ResourceTable<Dummy> table;
    PathRegistry<Dummy>  registry;

    auto handle = table.allocate(std::make_shared<Dummy>(7));
    registry.bind("brick.png", handle);

    auto found = registry.find("brick.png");
    EXPECT_TRUE(found.isValid());
    EXPECT_EQ(found, handle);
    EXPECT_EQ(registry.pathOf(handle), "brick.png");
}

TEST(PathRegistryTest, RebindOverwritesPreviousHandle)
{
    PathRegistry<Dummy> registry;

    FResourceHandle<Dummy> first{.index = 3, .generation = 1};
    FResourceHandle<Dummy> second{.index = 9, .generation = 2};

    registry.bind("tex.png", first);
    registry.bind("tex.png", second);

    EXPECT_EQ(registry.find("tex.png"), second);
}

TEST(PathRegistryTest, UnbindClearsBothDirections)
{
    PathRegistry<Dummy>    registry;
    FResourceHandle<Dummy> handle{.index = 4, .generation = 1};

    registry.bind("gone.png", handle);
    registry.unbind("gone.png");

    EXPECT_FALSE(registry.find("gone.png").isValid());
    EXPECT_TRUE(registry.pathOf(handle).empty());
}
