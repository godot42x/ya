#pragma once

#include "ForwardFrameGraphResources.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Forward/ForwardFrameResourceSet.h"
#include "Render3D/Forward/ForwardViewportStage.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ya
{

struct EntityIdViewportPass;
struct FrameContext;
struct PostProcessingStage;
struct RenderTargetCreateInfo;

namespace forward_frame_graph
{

struct ViewportGraphResources
{
    RGTextureHandle       color{};
    RGTextureHandle       resolve{};
    RGTextureHandle       depth{};
    RGTextureHandle       entityId{};
    std::optional<RGTextureHandle> shadowDepth{};
    Extent2D              viewportExtent{};
    AttachmentDescription colorAttachment{};
    AttachmentDescription depthAttachment{};
    AttachmentDescription entityIdAttachment{};
    Rect2D                renderArea{};
};

struct BuildInputs
{
    RenderGraph*                                     graph               = nullptr;
    RenderStageContext*                              stageCtx            = nullptr;
    ForwardFrameResourceSet::Binding                 frameBinding        = {};
    const RenderTargetCreateInfo*                    viewportRTSpec      = nullptr;
    std::vector<ForwardDirectionGizmoInput>          directionGizmos     = {};
    ForwardViewportStage::PassContext*               viewportPassContext = nullptr;
    FrameContext*                                    postContext         = nullptr;
    bool                                             bEnableShadow       = false;
    bool                                             bPostprocessOutputIsSRGB = false;
    std::shared_ptr<const RenderViewportOverlaySnapshot> viewportOverlaySnapshot = nullptr;
    uint64_t                                         viewId              = 0;
    const ForwardFrameResourceSet::ViewResources*    viewResources       = nullptr;
    std::optional<RGPassHandle>                      familyPredecessor   = std::nullopt;
};

struct Dependencies
{
    ForwardViewportStage* viewportStage = nullptr;
    EntityIdViewportPass* entityIdPass  = nullptr;
    PostProcessingStage*  postProcessStage = nullptr;
};

[[nodiscard]] ViewportGraphResources createViewportResources(
    RenderGraph& graph,
    const RenderTargetCreateInfo& viewportRTSpec,
    std::optional<RGTextureHandle> shadowDepth,
    uint64_t viewId);

void appendViewportPasses(RenderGraph& graph,
                          const Dependencies& deps,
                          const BuildInputs& inputs,
                          const ViewportGraphResources& resources);

void appendPostprocessPasses(RenderGraph& graph,
                             const Dependencies& deps,
                             const BuildInputs& inputs,
                             const ViewportGraphResources& resources);

void exportGraphOutputs(RenderGraph& graph, const ViewportGraphResources& resources, uint64_t viewId);

} // namespace forward_frame_graph

} // namespace ya
