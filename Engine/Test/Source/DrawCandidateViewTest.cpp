#include "Render/RenderFrameData.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <vector>

namespace ya
{

TEST(DrawCandidateViewTest, ExposesImmutableContiguousSnapshot)
{
    std::vector<RenderDrawItem> candidates(2);
    candidates[0].materialIndex = 7;
    candidates[1].materialIndex = 11;

    const DrawCandidateView view{std::span<const RenderDrawItem>(candidates)};

    EXPECT_FALSE(view.empty());
    EXPECT_EQ(view.size(), 2u);
    EXPECT_EQ(view[0].materialIndex, 7u);
    EXPECT_EQ(view[1].materialIndex, 11u);
    EXPECT_EQ(view.data(), candidates.data());
}

TEST(DrawCandidateViewTest, EmptyViewHasNoCandidates)
{
    const DrawCandidateView view{};

    EXPECT_TRUE(view.empty());
    EXPECT_EQ(view.size(), 0u);
    EXPECT_EQ(view.begin(), view.end());
    EXPECT_EQ(view.data(), nullptr);
}

TEST(DrawCandidateViewTest, DrawPacketCarriesGroupingContractWithoutOwningCandidates)
{
    std::vector<RenderDrawItem> candidates(2);
    candidates[0].materialIndex = 13;
    candidates[1].materialIndex = 13;

    DrawPacket packet{
        .candidates    = DrawCandidateView{std::span<const RenderDrawItem>(candidates)},
        .materialIndex = 13,
        .firstInstance = 4,
        .instanceCount = 2,
        .sortKey       = 2.5f,
        .bSkinned      = true,
    };

    EXPECT_FALSE(packet.isValid());
    EXPECT_EQ(packet.candidates.size(), 2u);
    EXPECT_EQ(packet.materialIndex, 13u);
    EXPECT_EQ(packet.firstInstance, 4u);
    EXPECT_EQ(packet.instanceCount, 2u);
    EXPECT_FLOAT_EQ(packet.sortKey, 2.5f);
    EXPECT_TRUE(packet.bSkinned);
}

static_assert(std::is_same_v<decltype(std::declval<DrawCandidateView>()[0]),
                             const RenderDrawItem&>);

} // namespace ya
