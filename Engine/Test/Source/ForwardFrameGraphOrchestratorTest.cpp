#include "Runtime/Rendering/Forward/ForwardFrameGraphOrchestrator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>
#include <utility>

namespace ya
{

namespace
{

bool hasDependency(const ForwardFrameGraphOrchestrator::TopologyDescription& topology,
                   std::string_view from,
                   std::string_view to)
{
    return std::find(topology.dependencies.begin(),
                     topology.dependencies.end(),
                     std::pair<std::string_view, std::string_view>{from, to}) !=
           topology.dependencies.end();
}

} // namespace

TEST(ForwardFrameGraphOrchestratorTest, DescribesFullForwardTopologyWithOptionalPasses)
{
    const auto topology = ForwardFrameGraphOrchestrator::describeTopology({
        .bHasShadowSubgraph  = true,
        .bHasBloomSubgraph   = true,
        .bHasPostprocessPass = true,
    });

    EXPECT_EQ(topology.passOrder.size(), 11u);
    EXPECT_EQ(topology.passOrder[0], "Shadow Subgraph");
    EXPECT_EQ(topology.passOrder[1], "Forward Skybox");
    EXPECT_EQ(topology.passOrder[2], "Forward PBR");
    EXPECT_EQ(topology.passOrder[3], "Forward Phong");
    EXPECT_EQ(topology.passOrder[4], "Forward Unlit");
    EXPECT_EQ(topology.passOrder[5], "Forward Simple");
    EXPECT_EQ(topology.passOrder[6], "Forward Direction");
    EXPECT_EQ(topology.passOrder[7], "Forward Debug");
    EXPECT_EQ(topology.passOrder[8], "Forward Viewport Overlay");
    EXPECT_EQ(topology.passOrder[9], "Bloom Subgraph");
    EXPECT_EQ(topology.passOrder[10], "Postprocessing");

    EXPECT_TRUE(hasDependency(topology, "Forward Skybox", "Forward PBR"));
    EXPECT_TRUE(hasDependency(topology, "Forward PBR", "Forward Phong"));
    EXPECT_TRUE(hasDependency(topology, "Forward Phong", "Forward Unlit"));
    EXPECT_TRUE(hasDependency(topology, "Forward Unlit", "Forward Simple"));
    EXPECT_TRUE(hasDependency(topology, "Forward Simple", "Forward Direction"));
    EXPECT_TRUE(hasDependency(topology, "Forward Direction", "Forward Debug"));
    EXPECT_TRUE(hasDependency(topology, "Forward Debug", "Forward Viewport Overlay"));
    EXPECT_TRUE(hasDependency(topology, "Shadow Subgraph", "Forward PBR"));
    EXPECT_TRUE(hasDependency(topology, "Shadow Subgraph", "Forward Phong"));
    EXPECT_TRUE(hasDependency(topology, "Forward Viewport Overlay", "Bloom Subgraph"));
    EXPECT_TRUE(hasDependency(topology, "Bloom Subgraph", "Postprocessing"));
}

TEST(ForwardFrameGraphOrchestratorTest, OmitsDisabledOptionalStagesFromTopology)
{
    const auto topology = ForwardFrameGraphOrchestrator::describeTopology({
        .bHasShadowSubgraph  = false,
        .bHasBloomSubgraph   = false,
        .bHasPostprocessPass = true,
    });

    EXPECT_EQ(topology.passOrder.size(), 9u);
    EXPECT_EQ(topology.passOrder[0], "Forward Skybox");
    EXPECT_EQ(topology.passOrder[1], "Forward PBR");
    EXPECT_EQ(topology.passOrder[2], "Forward Phong");
    EXPECT_EQ(topology.passOrder[3], "Forward Unlit");
    EXPECT_EQ(topology.passOrder[4], "Forward Simple");
    EXPECT_EQ(topology.passOrder[5], "Forward Direction");
    EXPECT_EQ(topology.passOrder[6], "Forward Debug");
    EXPECT_EQ(topology.passOrder[7], "Forward Viewport Overlay");
    EXPECT_EQ(topology.passOrder[8], "Postprocessing");

    EXPECT_EQ(std::find(topology.passOrder.begin(), topology.passOrder.end(), "Shadow Subgraph"),
              topology.passOrder.end());
    EXPECT_EQ(std::find(topology.passOrder.begin(), topology.passOrder.end(), "Bloom Subgraph"),
              topology.passOrder.end());
    EXPECT_NE(std::find(topology.passOrder.begin(), topology.passOrder.end(), "Postprocessing"),
              topology.passOrder.end());
    EXPECT_TRUE(hasDependency(topology, "Forward Viewport Overlay", "Postprocessing"));
}

TEST(ForwardFrameGraphOrchestratorTest, OmitsPostprocessWhenDisabled)
{
    const auto topology = ForwardFrameGraphOrchestrator::describeTopology({
        .bHasShadowSubgraph  = false,
        .bHasBloomSubgraph   = false,
        .bHasPostprocessPass = false,
    });

    EXPECT_EQ(topology.passOrder.size(), 8u);
    EXPECT_EQ(std::find(topology.passOrder.begin(), topology.passOrder.end(), "Postprocessing"),
              topology.passOrder.end());
}

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
