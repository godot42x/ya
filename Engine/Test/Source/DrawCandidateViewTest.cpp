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

static_assert(std::is_same_v<decltype(std::declval<DrawCandidateView>()[0]),
                             const RenderDrawItem&>);

} // namespace ya
