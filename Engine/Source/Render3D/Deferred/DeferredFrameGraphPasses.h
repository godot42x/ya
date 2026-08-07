#pragma once

#include "DeferredFrameGraphResources.h"
#include "DeferredFrameResourceSet.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "RHI/Core/RenderTargetCreateInfo.h"
#include "Render3D/Common/EntityIdViewportPass.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Deferred/GBufferStage.h"
#include "Render3D/Deferred/LightStage.h"
#include "Render3D/Deferred/SSAOStage.h"
#include "Render3D/Deferred/ViewportOverlayStage.h"

#include <array>
#include <memory>
#include <optional>

namespace ya
{

struct ShadowStage;
struct PostProcessingStage;

struct DeferredFrameGraphPassContext
{
    RenderGraph&                             graph;
    DeferredFrameGraphResources&             graphResources;
    const RenderStageContext&                stageCtx;
    const DeferredFrameResourceSet::Binding& frameBinding;
    const RenderTargetCreateInfo&            gBufferRTSpec;
    const RenderTargetCreateInfo&            viewportRTSpec;
    const ViewportOverlayStage::FrameInputs* overlayInputs = nullptr;
    const EnvironmentLightingSceneResources*  environmentLighting = nullptr;
    DescriptorSetHandle                      environmentLightingDS{};
    FrameContext*                            postContext = nullptr;
    Extent2D                                 viewportExtent{};
    bool                                     bUseSSAO = false;
    bool                                     bReverseViewportY = true;
    bool                                     bPostprocessOutputIsSRGB = false;
    std::shared_ptr<const RenderViewportOverlaySnapshot> viewportOverlaySnapshot = nullptr;

    ShadowStage*          shadowStage = nullptr;
    GBufferStage&         gBufferStage;
    LightStage&           lightStage;
    ViewportOverlayStage& overlayStage;
    PostProcessingStage&  postProcessStage;
    SSAOStage*            ssaoStage = nullptr;
    EntityIdViewportPass* entityIdPass = nullptr;
};

struct DeferredGBufferPassParams
{
    struct BufferInput
    {
        RGBufferHandle handle{};
        RGBufferRange  range{};
    };

    BufferInput                    frame{};
    BufferInput                    light{};
    RGBufferHandle                 skinning{};
    std::array<RGTextureHandle, 4> gBufferColors{};
    RGTextureHandle                gBufferDepth{};
    Rect2D                         renderArea{};
    uint32_t                       layerCount = 1;
    DescriptorSetHandle            frameAndLightDescriptorSet{};
    DescriptorSetHandle            skinningDescriptorSet{};
};

struct DeferredSSAOPassParams
{
    RGBufferHandle      frame{};
    RGBufferRange       frameRange{};
    RGTextureHandle     albedo{};
    RGTextureHandle     normal{};
    RGTextureHandle     depth{};
    RGTextureHandle     output{};
    DescriptorSetHandle frameDescriptorSet{};
};

struct DeferredLightPassParams
{
    struct BufferInput
    {
        RGBufferHandle handle{};
        RGBufferRange  range{};
    };

    BufferInput                    frame{};
    BufferInput                    light{};
    std::array<RGTextureHandle, 4> gBufferColors{};
    RGTextureHandle                gBufferDepth{};
    std::optional<RGTextureHandle> ssao{};
    std::optional<RGTextureHandle> environmentCubemap{};
    std::optional<RGTextureHandle> environmentIrradiance{};
    std::optional<RGTextureHandle> environmentPrefilter{};
    std::optional<RGTextureHandle> environmentBrdfLut{};
    std::optional<RGTextureHandle> shadowDepth{};
    RGTextureHandle                viewportColor{};
    Rect2D                         renderArea{};
    uint32_t                       layerCount = 1;
    DescriptorSetHandle            frameAndLightDescriptorSet{};
    DescriptorSetHandle            environmentLightingDescriptorSet{};
};

struct DeferredSkyboxPassParams
{
    struct BufferInput
    {
        RGBufferHandle handle{};
        RGBufferRange  range{};
    };

    BufferInput                                    frame{};
    RGTextureHandle                                viewportColor{};
    RGTextureHandle                                depth{};
    Rect2D                                         renderArea{};
    uint32_t                                       layerCount = 1;
    ViewportOverlayStage::FrameInputs::SkyboxInput skybox{};
};

struct DeferredForwardOpaquePassParams
{
    RGTextureHandle color{};
    RGTextureHandle depth{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
};

struct DeferredForwardTransparentPassParams
{
    RGTextureHandle                   color{};
    RGTextureHandle                   depth{};
    Rect2D                            renderArea{};
    uint32_t                          layerCount = 1;
    ViewportOverlayStage::FrameInputs overlay{};
};

struct DeferredOverlayPassParams
{
    RGTextureHandle                                      color{};
    RGTextureHandle                                      depth{};
    Rect2D                                               renderArea{};
    uint32_t                                             layerCount = 1;
    std::shared_ptr<const RenderViewportOverlaySnapshot> overlaySnapshot = nullptr;
    FrameContext                                         frameCtx{};
};

namespace deferred_frame_graph_passes
{

void importFrameBuffers(DeferredFrameGraphPassContext& context);
void createAttachmentTextures(DeferredFrameGraphPassContext& context);
void appendGBuffer(DeferredFrameGraphPassContext& context);
void appendSSAO(DeferredFrameGraphPassContext& context);
void appendLight(DeferredFrameGraphPassContext& context);
void appendForwardOpaque(DeferredFrameGraphPassContext& context);
void appendSkybox(DeferredFrameGraphPassContext& context);
void appendBloom(DeferredFrameGraphPassContext& context);
void appendForwardTransparent(DeferredFrameGraphPassContext& context);
void appendEntityId(DeferredFrameGraphPassContext& context);
void appendOverlay(DeferredFrameGraphPassContext& context);
void appendPostprocess(DeferredFrameGraphPassContext& context);

} // namespace deferred_frame_graph_passes

} // namespace ya
