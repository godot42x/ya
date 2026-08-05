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

    ForwardPBRPassParams pbr{};
    EXPECT_FALSE(pbr.viewportColor.isValid());
    EXPECT_FALSE(pbr.viewportDepth.isValid());
    EXPECT_EQ(pbr.layerCount, 1u);
    EXPECT_EQ(pbr.finalLayout, EImageLayout::ColorAttachmentOptimal);

    ForwardPhongPassParams phong{};
    EXPECT_FALSE(phong.viewportColor.isValid());
    EXPECT_FALSE(phong.viewportDepth.isValid());
    EXPECT_EQ(phong.layerCount, 1u);
    EXPECT_EQ(phong.finalLayout, EImageLayout::ColorAttachmentOptimal);

    ForwardUnlitPassParams unlit{};
    EXPECT_FALSE(unlit.viewportColor.isValid());
    EXPECT_FALSE(unlit.viewportDepth.isValid());
    EXPECT_EQ(unlit.layerCount, 1u);
    EXPECT_EQ(unlit.finalLayout, EImageLayout::ColorAttachmentOptimal);

    ForwardSimplePassParams simple{};
    EXPECT_FALSE(simple.viewportColor.isValid());
    EXPECT_FALSE(simple.viewportDepth.isValid());
    EXPECT_EQ(simple.layerCount, 1u);
    EXPECT_EQ(simple.finalLayout, EImageLayout::ColorAttachmentOptimal);

    ForwardDirectionPassParams direction{};
    EXPECT_FALSE(direction.viewportColor.isValid());
    EXPECT_FALSE(direction.viewportDepth.isValid());
    EXPECT_EQ(direction.layerCount, 1u);
    EXPECT_EQ(direction.finalLayout, EImageLayout::ColorAttachmentOptimal);
    EXPECT_TRUE(direction.directionGizmos.empty());

    ForwardDebugPassParams debug{};
    EXPECT_FALSE(debug.viewportColor.isValid());
    EXPECT_FALSE(debug.viewportDepth.isValid());
    EXPECT_EQ(debug.layerCount, 1u);
    EXPECT_EQ(debug.finalLayout, EImageLayout::ColorAttachmentOptimal);

    ForwardViewportOverlayPassParams overlay{};
    EXPECT_FALSE(overlay.viewportColor.isValid());
    EXPECT_FALSE(overlay.viewportDepth.isValid());
    EXPECT_EQ(overlay.layerCount, 1u);
    EXPECT_EQ(overlay.finalLayout, EImageLayout::ColorAttachmentOptimal);
    EXPECT_FALSE(overlay.recordViewportOverlays);
}

} // namespace ya
