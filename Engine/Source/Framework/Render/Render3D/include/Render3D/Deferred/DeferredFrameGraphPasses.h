#pragma once

#include "Render3D/Deferred/DeferredFrameGraphResources.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "RHI/Core/RenderTargetCreateInfo.h"
#include "Render3D/Common/EntityIdPass.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Deferred/GBufferStage.h"
#include "Render3D/Deferred/LightStage.h"
#include "Render3D/Deferred/SSAOStage.h"
#include "Render3D/Deferred/ViewOverlayStage.h"

#include <array>
#include <cstdint>
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
    const ViewOverlayStage::FrameInputs* overlayInputs = nullptr;
    const DeferredFrameResourceSet::ViewResources* viewResources = nullptr;
    const EnvironmentLightingSceneResources*  environmentLighting = nullptr;
    DescriptorSetHandle                      environmentLightingDS{};
    FrameContext*                            postContext = nullptr;
    Extent2D                                 viewportExtent{};
    bool                                     bUseSSAO = false;
    bool                                     bReverseViewportY = true;
    bool                                     bPostprocessOutputIsSRGB = false;
    uint64_t                                 viewId = 0;
    std::optional<RGPassHandle>              familyPredecessor = std::nullopt;

    ShadowStage*          shadowStage = nullptr;
    GBufferStage&         gBufferStage;
    LightStage&           lightStage;
    ViewOverlayStage& overlayStage;
    PostProcessingStage&  postProcessStage;
    SSAOStage*            ssaoStage = nullptr;
    EntityIdPass* entityIdPass = nullptr;
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
    DescriptorSetHandle inputDescriptorSet{};
    uint64_t            viewId = 0;
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
    DescriptorSetHandle            gBufferTextureDescriptorSet{};
    DescriptorSetHandle            shadowDescriptorSet{};
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
    ViewOverlayStage::FrameInputs::SkyboxInput skybox{};
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
    ViewOverlayStage::FrameInputs overlay{};
    OverlayPassBindings               overlayBindings{};
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
void appendPostprocess(DeferredFrameGraphPassContext& context);

} // namespace deferred_frame_graph_passes

} // namespace ya
