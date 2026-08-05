#include "Runtime/Rendering/Forward/ForwardFrameGraphOrchestrator.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(ForwardPassParamsTest, ViewportPassParamsDefaultsStayEmpty)
{
    ForwardSkyboxPassParams skybox{};
    EXPECT_FALSE(skybox.viewportColor.isValid());
    EXPECT_FALSE(skybox.viewportDepth.isValid());
    EXPECT_EQ(skybox.layerCount, 1u);
    EXPECT_EQ(skybox.finalLayout, EImageLayout::ColorAttachmentOptimal);

    ForwardOpaquePassParams opaque{};
    EXPECT_FALSE(opaque.viewportColor.isValid());
    EXPECT_FALSE(opaque.viewportDepth.isValid());
    EXPECT_EQ(opaque.layerCount, 1u);
    EXPECT_EQ(opaque.finalLayout, EImageLayout::ColorAttachmentOptimal);
    EXPECT_TRUE(opaque.directionGizmos.empty());

    ForwardTransparentPassParams transparent{};
    EXPECT_FALSE(transparent.viewportColor.isValid());
    EXPECT_FALSE(transparent.viewportDepth.isValid());
    EXPECT_EQ(transparent.layerCount, 1u);
    EXPECT_EQ(transparent.finalLayout, EImageLayout::ColorAttachmentOptimal);

    ForwardOverlayPassParams overlay{};
    EXPECT_FALSE(overlay.viewportColor.isValid());
    EXPECT_FALSE(overlay.viewportDepth.isValid());
    EXPECT_EQ(overlay.layerCount, 1u);
    EXPECT_EQ(overlay.finalLayout, EImageLayout::ColorAttachmentOptimal);
    EXPECT_FALSE(overlay.overlaySnapshot);
}

} // namespace ya
