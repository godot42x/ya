#include "Runtime/App/RenderRuntime.h"

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
    EXPECT_TRUE(viewport.debugSpec.slots.empty());
    EXPECT_TRUE(viewport.debugSpec.groups.empty());
    EXPECT_TRUE(targets.entries.empty());
}

} // namespace
} // namespace ya
