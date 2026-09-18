#include "Render3D/Common/Shadow/BasicShadowMap/BasicShadowMapTechnique.h"
#include "Render3D/RenderFrameData.h"

#include "Graph/RenderGraph.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

} // namespace

TEST(ShadowPreparedViewTest, DefaultTokenIsInvalid)
{
    EXPECT_FALSE(ShadowPreparedView{}.valid());
    EXPECT_EQ(ShadowPreparedView{}.viewSlot, RenderViewRecordingContext::kInvalidViewSlot);

    const ShadowPreparedView prepared{.viewSlot = 3, .pointLightCount = 2};
    EXPECT_TRUE(prepared.valid());
    EXPECT_EQ(prepared.viewSlot, 3u);
    EXPECT_EQ(prepared.pointLightCount, 2u);
}

TEST(ShadowPreparedViewTest, UnpreparedViewAppendsNoShadowPasses)
{
    BasicShadowMapTechnique technique;
    // Shadows are on, so the only reason to append nothing is the token: an
    // unprepared View must not inherit whichever View was prepared before it.
    ASSERT_TRUE(technique.getSettings().isEnabled());

    RenderGraph    graph;
    RenderFrameData frameData;
    const size_t   passesBefore = graph.getPasses().size();

    const ShadowGraphOutputs outputs =
        technique.appendGraphPasses(graph, 0, frameData, ShadowPreparedView{});

    EXPECT_FALSE(outputs.shadowDepth.has_value());
    EXPECT_EQ(graph.getPasses().size(), passesBefore);
}

} // namespace ya
