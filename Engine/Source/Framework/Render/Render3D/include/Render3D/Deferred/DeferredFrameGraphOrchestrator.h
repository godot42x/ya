#pragma once

#include "Render3D/Deferred/DeferredFrameGraphResources.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/Deferred/ViewOverlayStage.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Shadow/IShadowTechnique.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace ya
{

struct FrameContext;
struct ShadowStage;
struct GBufferStage;
struct LightStage;
struct SSAOStage;
struct PostProcessingStage;
struct RenderTargetCreateInfo;
struct EntityIdPass;
struct Sprite2DStage;
struct ViewTargetLease;

struct DeferredFrameGraphOrchestrator
{
    struct BuildDependencies
    {
        ShadowStage*          shadowStage          = nullptr;
        GBufferStage*         gBufferStage         = nullptr;
        LightStage*           lightStage           = nullptr;
        ViewOverlayStage* overlayStage         = nullptr;
        PostProcessingStage*  postProcessStage     = nullptr;
        SSAOStage*            ssaoStage            = nullptr;
        EntityIdPass* entityIdPass         = nullptr;
        Sprite2DStage* spriteStage         = nullptr;
    };

    struct BuildInputs
    {
        RenderGraph*                           graph                     = nullptr;
        DeferredFrameGraphResources*           graphResources            = nullptr;
        const RenderStageContext*              stageCtx                  = nullptr;
        const DeferredFrameResourceSet::Binding* frameBinding            = nullptr;
        const RenderPipelineFrameContext*      frame                     = nullptr;
        const RenderTargetCreateInfo*          gBufferRTSpec             = nullptr;
        const RenderTargetCreateInfo*          viewRTSpec            = nullptr;
        const ViewOverlayStage::FrameInputs* overlayInputs           = nullptr;
        const EnvironmentLightingSceneResources* environmentLighting     = nullptr;
        DescriptorSetHandle                    environmentLightingDS     = nullptr;
        FrameContext*                          postContext               = nullptr;
        Extent2D                               viewExtent            {};
        /// What the View's shadow preparation produced. Passing it in keeps the
        /// stage from remembering which View it prepared last.
        ShadowPreparedView                     shadowPrepared            {};
        bool                                   bUseSSAO                  = false;
        bool                                   bReverseViewportY         = true;
        bool                                   bPostprocessOutputIsSRGB  = false;
        uint64_t                               viewId                    = 0;
        const DeferredFrameResourceSet::ViewResources* viewResources     = nullptr;
        const ViewTargetLease*                  targets                  = nullptr;
        std::optional<RGPassHandle>            familyPredecessor         = std::nullopt;
    };

    void build(const BuildDependencies& deps, const BuildInputs& inputs) const;

  private:
    void exportGraphOutputs(RenderGraph& graph, const DeferredFrameGraphResources& resources, uint64_t viewId) const;
};

} // namespace ya
