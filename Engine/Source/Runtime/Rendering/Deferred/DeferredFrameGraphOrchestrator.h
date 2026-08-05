#pragma once

#include "DeferredFrameGraphResources.h"
#include "DeferredFrameResourceSet.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Runtime/Rendering/Common/IRenderPipeline.h"

#include <functional>
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

struct DeferredFrameGraphOrchestrator
{
    struct BuildDependencies
    {
        ShadowStage*          shadowStage          = nullptr;
        GBufferStage*         gBufferStage         = nullptr;
        LightStage*           lightStage           = nullptr;
        ViewportOverlayStage* overlayStage         = nullptr;
        PostProcessingStage*  postProcessStage     = nullptr;
        SSAOStage*            ssaoStage            = nullptr;
    };

    struct BuildInputs
    {
        RenderGraph*                           graph                     = nullptr;
        DeferredFrameGraphResources*           graphResources            = nullptr;
        const RenderStageContext*              stageCtx                  = nullptr;
        const DeferredFrameResourceSet::Binding* frameBinding            = nullptr;
        const RenderPipelineFrameContext*      frame                     = nullptr;
        const RenderTargetCreateInfo*          gBufferRTSpec             = nullptr;
        const RenderTargetCreateInfo*          viewportRTSpec            = nullptr;
        const ViewportOverlayStage::FrameInputs* overlayInputs           = nullptr;
        const EnvironmentLightingSceneResources* environmentLighting     = nullptr;
        DescriptorSetHandle                    environmentLightingDS     = nullptr;
        FrameContext*                          postContext               = nullptr;
        Extent2D                               viewportExtent            {};
        bool                                   bUseSSAO                  = false;
        bool                                   bReverseViewportY         = true;
        bool                                   bPostprocessOutputIsSRGB  = false;
        std::shared_ptr<const RenderViewportOverlaySnapshot> viewportOverlaySnapshot = nullptr;
    };

    void build(const BuildDependencies& deps, const BuildInputs& inputs) const;
};

} // namespace ya
