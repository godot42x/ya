#include "Render3D/Common/ViewPersistentResourceKey.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(ViewPersistentResourceKeyTest, SameViewIdProducesSameKey)
{
    const auto a = makeViewPersistentTextureKey("ForwardViewport.Color", 11);
    const auto b = makeViewPersistentTextureKey("ForwardViewport.Color", 11);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.value, "ForwardViewport.Color.view11");
}

TEST(ViewPersistentResourceKeyTest, DifferentViewIdsDoNotAlias)
{
    const auto a = makeViewPersistentTextureKey("ForwardViewport.Color", 11);
    const auto b = makeViewPersistentTextureKey("ForwardViewport.Color", 12);
    EXPECT_NE(a, b);
    EXPECT_EQ(b.value, "ForwardViewport.Color.view12");
}

TEST(ViewPersistentResourceKeyTest, MissingViewStillNamespacesZero)
{
    const auto key = makeViewPersistentTextureKey("DeferredGBuffer.Depth", 0);
    EXPECT_EQ(key.value, "DeferredGBuffer.Depth.view0");
    EXPECT_NE(key.value, "DeferredGBuffer.Depth");
}

TEST(ViewPersistentResourceKeyTest, BasesStayIndependentForTheSameView)
{
    const auto color = makeViewPersistentTextureKey("DeferredViewport.Color", 7);
    const auto ssao  = makeViewPersistentTextureKey("SSAO.Output", 7);
    EXPECT_NE(color, ssao);
    EXPECT_EQ(color.value, "DeferredViewport.Color.view7");
    EXPECT_EQ(ssao.value, "SSAO.Output.view7");
}

} // namespace ya
