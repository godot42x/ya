#include "Render3D/RenderFrameData.h"

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

TEST(DrawCandidateViewTest, DrawPacketGroupingPreservesSortedOrder)
{
    auto* meshA = reinterpret_cast<Mesh*>(static_cast<uintptr_t>(1));
    auto* meshB = reinterpret_cast<Mesh*>(static_cast<uintptr_t>(2));
    auto* matA  = reinterpret_cast<Material*>(static_cast<uintptr_t>(3));
    auto* matB  = reinterpret_cast<Material*>(static_cast<uintptr_t>(4));

    std::vector<RenderDrawItem> candidates(4);
    candidates[0] = RenderDrawItem{.mesh = meshA, .material = matA, .materialIndex = 1, .sortKey = 1.0f};
    candidates[1] = RenderDrawItem{.mesh = meshA, .material = matA, .materialIndex = 1, .sortKey = 2.0f};
    candidates[2] = RenderDrawItem{.mesh = meshB, .material = matB, .materialIndex = 2, .sortKey = 3.0f};
    candidates[3] = RenderDrawItem{.mesh = meshA, .material = matA, .materialIndex = 1, .sortKey = 4.0f};

    const auto packets = buildDrawPackets(
        DrawCandidateView{std::span<const RenderDrawItem>(candidates)},
        false);

    ASSERT_EQ(packets.size(), 3u);
    EXPECT_EQ(packets[0].candidates.size(), 2u);
    EXPECT_EQ(packets[0].firstInstance, 0u);
    EXPECT_EQ(packets[0].instanceCount, 2u);
    EXPECT_EQ(packets[0].mesh, meshA);
    EXPECT_EQ(packets[1].candidates.size(), 1u);
    EXPECT_EQ(packets[1].firstInstance, 2u);
    EXPECT_EQ(packets[1].mesh, meshB);
    EXPECT_EQ(packets[2].candidates.size(), 1u);
    EXPECT_EQ(packets[2].firstInstance, 3u);
    EXPECT_EQ(packets[2].mesh, meshA);
    EXPECT_FALSE(packets[0].bSkinned);
}

static_assert(std::is_same_v<decltype(std::declval<DrawCandidateView>()[0]),
                             const RenderDrawItem&>);

} // namespace ya
