#include "Render2D/TextureTableBatch.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace ya
{

namespace
{

struct Candidate
{
    int id  = 0;
    int tex = 0;
};

struct Instance
{
    int      id   = 0;
    uint32_t slot = 0;
};

TextureTableKey keyFor(int tex)
{
    return TextureTableKey{.identity = reinterpret_cast<const void*>(static_cast<uintptr_t>(tex))};
}

InstancedDrawPlan<Instance, int> planOf(const std::vector<Candidate>& candidates, uint32_t capacity = TextureTableCursor::kDefaultCapacity)
{
    return planInstancedDraws<std::vector<Candidate>, Instance, int>(
        candidates,
        0,
        keyFor(0),
        [](const Candidate& candidate) { return keyFor(candidate.tex); },
        [](const Candidate& candidate) { return candidate.tex; },
        [](const Candidate& candidate, uint32_t slot, Instance& instance) {
            instance.id   = candidate.id;
            instance.slot = slot;
        },
        capacity);
}

} // namespace

TEST(TextureTableBatchTest, OrderIsTheCandidateOrder)
{
    const auto plan = planOf({
        {.id = 3, .tex = 2},
        {.id = 1, .tex = 9},
        {.id = 2, .tex = 4},
    });
    ASSERT_EQ(plan.instances.size(), 3u);
    EXPECT_EQ(plan.instances[0].id, 3);
    EXPECT_EQ(plan.instances[1].id, 1);
    EXPECT_EQ(plan.instances[2].id, 2);
    ASSERT_EQ(plan.batches.size(), 1u);
}

TEST(TextureTableBatchTest, ConsecutiveSameTextureIsOneBatch)
{
    const auto plan = planOf({
        {.id = 0, .tex = 4},
        {.id = 1, .tex = 4},
        {.id = 2, .tex = 4},
        {.id = 3, .tex = 4},
    });
    ASSERT_EQ(plan.batches.size(), 1u);
    EXPECT_EQ(plan.batches[0].count, 4u);
    EXPECT_EQ(plan.batches[0].slots.size(), 2u);
    EXPECT_EQ(plan.instances[0].slot, 1u);
    EXPECT_EQ(plan.instances[3].slot, 1u);
}

TEST(TextureTableBatchTest, InterleavedTexturesThatFitStayOneBatch)
{
    // A texture-change cut would make three batches. The table holds both, so
    // the already-sorted order stays one batch and the second A reuses its slot.
    const auto plan = planOf({
        {.id = 0, .tex = 1},
        {.id = 1, .tex = 2},
        {.id = 2, .tex = 1},
    });
    ASSERT_EQ(plan.batches.size(), 1u);
    EXPECT_EQ(plan.instances[0].slot, 1u);
    EXPECT_EQ(plan.instances[1].slot, 2u);
    EXPECT_EQ(plan.instances[2].slot, 1u);
}

TEST(TextureTableBatchTest, FullTableCutsAndRemapsTheNextBatch)
{
    std::vector<Candidate> candidates;
    for (int tex = 1; tex <= 16; ++tex) {
        candidates.push_back({.id = tex, .tex = tex});
    }
    candidates.push_back({.id = 100, .tex = 1});

    const auto plan = planOf(candidates);
    ASSERT_EQ(plan.instances.size(), 17u);
    ASSERT_EQ(plan.batches.size(), 2u);
    EXPECT_EQ(plan.batches[0].first, 0u);
    EXPECT_EQ(plan.batches[0].count, 15u);
    EXPECT_EQ(plan.batches[0].slots.size(), 16u);
    EXPECT_EQ(plan.batches[1].first, 15u);
    EXPECT_EQ(plan.batches[1].count, 2u);
    // The texture that did not fit is slot 1 of the new table, not dropped.
    EXPECT_EQ(plan.instances[15].id, 16);
    EXPECT_EQ(plan.instances[15].slot, 1u);
    EXPECT_EQ(plan.batches[1].slots[1], 16);
    // Texture 1 was slot 1 in the first batch and is remapped in the second.
    EXPECT_EQ(plan.instances[0].slot, 1u);
    EXPECT_EQ(plan.instances[16].slot, 2u);
    EXPECT_EQ(plan.batches[1].slots[2], 1);
}

TEST(TextureTableBatchTest, MoreThanSixteenTexturesAreAllDrawn)
{
    std::vector<Candidate> candidates;
    candidates.reserve(20);
    for (int tex = 1; tex <= 20; ++tex) {
        candidates.push_back({.id = tex, .tex = tex});
    }
    const auto plan = planOf(candidates);
    ASSERT_EQ(plan.instances.size(), 20u);
    ASSERT_EQ(plan.batches.size(), 2u);
    EXPECT_EQ(plan.batches[0].count, 15u);
    EXPECT_EQ(plan.batches[1].count, 5u);
    for (int index = 0; index < 20; ++index) {
        EXPECT_EQ(plan.instances[static_cast<size_t>(index)].id, index + 1);
    }
}

TEST(TextureTableBatchTest, SlotZeroIsTheWhiteKey)
{
    const auto plan = planOf({
        {.id = 0, .tex = 0},
        {.id = 1, .tex = 3},
        {.id = 2, .tex = 0},
    });
    ASSERT_EQ(plan.batches.size(), 1u);
    EXPECT_EQ(plan.batches[0].slots[0], 0);
    EXPECT_EQ(plan.batches[0].slots.size(), 2u);
    EXPECT_EQ(plan.instances[0].slot, 0u);
    EXPECT_EQ(plan.instances[1].slot, 1u);
    EXPECT_EQ(plan.instances[2].slot, 0u);
}

TEST(TextureTableBatchTest, LookupDoesNotScanTheLiveTable)
{
    auto measure = [](int kinds) {
        std::vector<Candidate> candidates;
        candidates.reserve(4000);
        for (int index = 0; index < 4000; ++index) {
            candidates.push_back({.id = index, .tex = (index % kinds) + 1});
        }
        return planOf(candidates).keyComparisons;
    };

    const uint64_t few  = measure(2);
    const uint64_t many = measure(15);
    // A linear scan of a 15-wide table is several times a 2-wide scan. The
    // direct map stays on the order of one probe per sprite either way.
    EXPECT_LT(many, few * 3u);
    EXPECT_LT(many, 4000u * 4u);
}

TEST(TextureTableBatchTest, BatchKeepsTheSlotValueAfterTheCandidateReleasesIt)
{
    auto resource = std::make_shared<int>(7);
    struct Held
    {
        int                  tex = 1;
        std::shared_ptr<int> resource;
    };
    std::vector<Held> candidates{{.tex = 1, .resource = resource}};
    const auto plan = planInstancedDraws<std::vector<Held>, Instance, std::shared_ptr<int>>(
        candidates,
        std::shared_ptr<int>{},
        keyFor(0),
        [](const Held& candidate) { return keyFor(candidate.tex); },
        [](const Held& candidate) { return candidate.resource; },
        [](const Held&, uint32_t slot, Instance& instance) { instance.slot = slot; });

    candidates.clear();
    resource.reset();
    ASSERT_EQ(plan.batches.size(), 1u);
    ASSERT_EQ(plan.batches[0].slots.size(), 2u);
    ASSERT_TRUE(plan.batches[0].slots[1]);
    EXPECT_EQ(*plan.batches[0].slots[1], 7);
    EXPECT_EQ(plan.batches[0].slots[1].use_count(), 1u);
}

} // namespace ya
