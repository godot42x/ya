#include "Render3D/Common/ViewPersistentResourceKey.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(ViewPersistentResourceKeyTest, SameViewIdProducesSameKey)
{
    const auto a = makeViewPersistentTextureKey("ForwardView.Color", 11);
    const auto b = makeViewPersistentTextureKey("ForwardView.Color", 11);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.value, "ForwardView.Color.view11");
}

TEST(ViewPersistentResourceKeyTest, DifferentViewIdsDoNotAlias)
{
    const auto a = makeViewPersistentTextureKey("ForwardView.Color", 11);
    const auto b = makeViewPersistentTextureKey("ForwardView.Color", 12);
    EXPECT_NE(a, b);
    EXPECT_EQ(b.value, "ForwardView.Color.view12");
}

TEST(ViewPersistentResourceKeyTest, MissingViewStillNamespacesZero)
{
    const auto key = makeViewPersistentTextureKey("DeferredGBuffer.Depth", 0);
    EXPECT_EQ(key.value, "DeferredGBuffer.Depth.view0");
    EXPECT_NE(key.value, "DeferredGBuffer.Depth");
}

TEST(ViewPersistentResourceKeyTest, BasesStayIndependentForTheSameView)
{
    const auto color = makeViewPersistentTextureKey("DeferredView.Color", 7);
    const auto ssao  = makeViewPersistentTextureKey("SSAO.Output", 7);
    EXPECT_NE(color, ssao);
    EXPECT_EQ(color.value, "DeferredView.Color.view7");
    EXPECT_EQ(ssao.value, "SSAO.Output.view7");
}

TEST(ViewPersistentResourceKeyTest, PostprocessAndBloomOutputsStayViewKeyed)
{
    const auto postA = makeViewPersistentTextureKey("Postprocessing.Output", 11);
    const auto postB = makeViewPersistentTextureKey("Postprocessing.Output", 12);
    const auto bloomA = makeViewPersistentTextureKey("Bloom.CompositeOutput", 11);
    const auto bloomB = makeViewPersistentTextureKey("Bloom.CompositeOutput", 12);
    const auto extractA = makeViewPersistentTextureKey("Bloom.Extract", 11);

    EXPECT_NE(postA, postB);
    EXPECT_NE(bloomA, bloomB);
    EXPECT_NE(postA, bloomA);
    EXPECT_NE(bloomA, extractA);
    EXPECT_EQ(postA.value, "Postprocessing.Output.view11");
    EXPECT_EQ(postB.value, "Postprocessing.Output.view12");
    EXPECT_EQ(bloomA.value, "Bloom.CompositeOutput.view11");
    EXPECT_EQ(extractA.value, "Bloom.Extract.view11");
}

} // namespace ya
