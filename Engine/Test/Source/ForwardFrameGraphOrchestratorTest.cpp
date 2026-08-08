#include "Framework/Game/Render/Render3D/Forward/ForwardFrameGraphOrchestrator.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(ForwardGraphInputsTest, BuildInputsDefaultsStayEmpty)
{
    const forward_frame_graph::BuildInputs inputs{};
    EXPECT_EQ(inputs.graph, nullptr);
    EXPECT_EQ(inputs.stageCtx, nullptr);
    EXPECT_EQ(inputs.viewportRTSpec, nullptr);
    EXPECT_EQ(inputs.postContext, nullptr);
    EXPECT_TRUE(inputs.directionGizmos.empty());
    EXPECT_FALSE(inputs.bEnableShadow);
    EXPECT_FALSE(inputs.bPostprocessOutputIsSRGB);
    EXPECT_FALSE(inputs.viewportOverlaySnapshot);

    const forward_frame_graph::ViewportGraphResources resources{};
    EXPECT_FALSE(resources.color.isValid());
    EXPECT_FALSE(resources.resolve.isValid());
    EXPECT_FALSE(resources.depth.isValid());
    EXPECT_FALSE(resources.entityId.isValid());
    EXPECT_FALSE(resources.shadowDepth.has_value());
}

} // namespace ya
