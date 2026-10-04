#include "Scene2D/SpriteDrawOrder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace ya
{
namespace
{

SpriteDrawKey keyWith(int32_t layer, int32_t rank, float yKey, int32_t order, uint32_t entityId, uint32_t sequence)
{
    SpriteDrawKey key;
    key.layer     = layer;
    key.ySortRank = rank;
    key.yKey      = yKey;
    key.order     = order;
    key.entityId  = entityId;
    key.sequence  = sequence;
    return key;
}

void expectLater(const SpriteDrawKey& earlier, const SpriteDrawKey& later)
{
    EXPECT_TRUE(spriteDrawsBefore(earlier, later));
    EXPECT_FALSE(spriteDrawsBefore(later, earlier));
}

} // namespace

TEST(SpriteDrawOrderTest, FieldsApplyInPriorityOrder)
{
    const SpriteDrawKey base = keyWith(0, 0, 0.0f, 0, 0, 0);
    expectLater(base, keyWith(1, 0, -100.0f, -5, 0, 0));
    expectLater(keyWith(0, 0, 50.0f, 9, 9, 9), keyWith(0, 1, -50.0f, 0, 0, 0));
    expectLater(keyWith(0, 1, -1.0f, 0, 0, 0), keyWith(0, 1, 0.0f, 0, 0, 0));
    expectLater(keyWith(0, 1, 0.0f, 1, 0, 0), keyWith(0, 1, 0.0f, 2, 0, 0));
    expectLater(keyWith(0, 0, 0.0f, 0, 3, 9), keyWith(0, 0, 0.0f, 0, 4, 0));
    expectLater(keyWith(0, 0, 0.0f, 0, 4, 1), keyWith(0, 0, 0.0f, 0, 4, 2));
}

TEST(SpriteDrawOrderTest, YSortOffForcesAZeroYKey)
{
    const SpriteDrawKey off = makeSpriteDrawKey(0, false, 4.0f, 0, 1, 0);
    const SpriteDrawKey on  = makeSpriteDrawKey(0, true, 4.0f, 0, 1, 0);
    EXPECT_EQ(off.ySortRank, 0);
    EXPECT_FLOAT_EQ(off.yKey, 0.0f);
    EXPECT_EQ(on.ySortRank, 1);
    EXPECT_FLOAT_EQ(on.yKey, -4.0f);
    expectLater(off, on);
}

TEST(SpriteDrawOrderTest, EqualKeysAreEquivalentAndStableSortKeepsInputOrder)
{
    const SpriteDrawKey key = keyWith(2, 1, -0.5f, 3, 8, 1);
    EXPECT_FALSE(spriteDrawsBefore(key, key));

    std::vector<int> order{2, 0, 1};
    std::stable_sort(order.begin(), order.end(), [&](int lhs, int rhs) {
        return spriteDrawsBefore(key, key) && lhs < rhs;
    });
    EXPECT_EQ(order, (std::vector<int>{2, 0, 1}));

    std::vector<SpriteDrawKey> keys{
        keyWith(0, 0, 0.0f, 0, 1, 2),
        keyWith(0, 0, 0.0f, 0, 1, 0),
        keyWith(0, 0, 0.0f, 0, 1, 1),
    };
    std::vector<int> indexes{0, 1, 2};
    std::stable_sort(indexes.begin(), indexes.end(), [&](int lhs, int rhs) {
        return spriteDrawsBefore(keys[static_cast<size_t>(lhs)], keys[static_cast<size_t>(rhs)]);
    });
    EXPECT_EQ(indexes, (std::vector<int>{1, 2, 0}));
}

} // namespace ya
