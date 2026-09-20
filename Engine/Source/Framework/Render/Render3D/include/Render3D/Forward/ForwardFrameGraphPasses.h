#pragma once

#include "Render3D/Forward/ForwardFrameGraphResources.h"
#include "Render3D/Forward/ForwardFrameResourceSet.h"
#include "Render3D/Forward/ForwardViewStage.h"
#include "Render3D/Shadow/IShadowTechnique.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ya
{

struct EntityIdPass;
struct FrameContext;
struct PostProcessingStage;
struct RenderTargetCreateInfo;

namespace forward_frame_graph
{

struct ViewGraphResources
{
    RGTextureHandle       color{};
    RGTextureHandle       resolve{};
    RGTextureHandle       depth{};
    RGTextureHandle       entityId{};
    std::optional<RGTextureHandle> shadowDepth{};
    Extent2D              viewExtent{};
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
    const RenderTargetCreateInfo*                    viewRTSpec      = nullptr;
    std::vector<ForwardDirectionGizmoInput>          directionGizmos     = {};
    ForwardViewStage::PassContext*               viewPassContext = nullptr;
    FrameContext*                                    postContext         = nullptr;
    bool                                             bEnableShadow       = false;
    /// What the View's shadow preparation produced; invalid means no shadow
    /// passes for this View, so the stage never has to remember one.
    ShadowPreparedView                               shadowPrepared      = {};
    bool                                             bPostprocessOutputIsSRGB = false;
    uint64_t                                         viewId              = 0;
    const ForwardFrameResourceSet::ViewResources*    viewResources       = nullptr;
    std::optional<RGPassHandle>                      familyPredecessor   = std::nullopt;
};

struct Dependencies
{
    ForwardViewStage* viewStage = nullptr;
    EntityIdPass* entityIdPass  = nullptr;
    PostProcessingStage*  postProcessStage = nullptr;
};

[[nodiscard]] ViewGraphResources createViewResources(
    RenderGraph& graph,
    const RenderTargetCreateInfo& viewRTSpec,
    std::optional<RGTextureHandle> shadowDepth,
    uint64_t viewId);

void appendViewPasses(RenderGraph& graph,
                          const Dependencies& deps,
                          const BuildInputs& inputs,
                          const ViewGraphResources& resources);

void appendPostprocessPasses(RenderGraph& graph,
                             const Dependencies& deps,
                             const BuildInputs& inputs,
                             const ViewGraphResources& resources);

void exportGraphOutputs(RenderGraph& graph, const ViewGraphResources& resources, uint64_t viewId);

} // namespace forward_frame_graph

} // namespace ya
