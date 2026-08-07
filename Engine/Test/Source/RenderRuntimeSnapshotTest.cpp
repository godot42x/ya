#include "Render3D/RenderRuntime.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

TEST(RenderRuntimeSnapshotTest, EmptyRuntimePublishesEmptyViewportResources)
{
    RenderRuntime runtime;

    const RenderViewportSnapshot viewport = runtime.buildViewportSnapshot();
    const RenderTargetCatalog    targets  = runtime.buildRenderTargetCatalog();

    EXPECT_EQ(viewport.viewportImageOwner, nullptr);
    EXPECT_EQ(viewport.viewportImageView, nullptr);
    EXPECT_FALSE(viewport.bPostprocessingEnabled);
    ASSERT_NE(viewport.debugCatalog, nullptr);
    EXPECT_FALSE(viewport.debugCatalog->categories.empty());
    EXPECT_TRUE(viewport.debugCatalog->slots.empty());
    EXPECT_TRUE(viewport.debugCatalog->groups.empty());
    EXPECT_TRUE(viewport.debugImages.empty());
    EXPECT_TRUE(targets.entries.empty());
}

} // namespace
} // namespace ya
