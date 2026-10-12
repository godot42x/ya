#pragma once

#include "Render3D/Deferred/DeferredFrameGraphResources.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "RHI/Core/RenderTargetCreateInfo.h"
#include "Render3D/Common/EntityIdPass.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/Sprite2DStage.h"
#include "Render3D/Deferred/ViewOverlayStage.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace ya
{

struct ShadowStage;
struct PostProcessingStage;
struct ViewTargetLease;

/// The View-scoped context of the Deferred frame plumbing that is not (yet) a
/// feature: the host-written buffer imports and the tail passes after lighting.
/// The three composition passes (GBuffer/SSAO/Lighting) belong to their
/// features and read a FeatureGraphContext instead.
struct DeferredFrameGraphPassContext
{
    RenderGraph&                             graph;
    DeferredFrameGraphResources&             graphResources;
    const RenderStageContext&                stageCtx;
    const DeferredFrameResourceSet::Binding& frameBinding;
    const DeferredFrameResourceSet::ViewResources* viewResources = nullptr;
    const ViewTargetLease*                  targets = nullptr;
    const ViewOverlayStage::FrameInputs*    overlayInputs = nullptr;
    FrameContext*                           postContext = nullptr;
    Extent2D                                viewExtent{};
    bool                                    bPostprocessOutputIsSRGB = false;
    uint64_t                                viewId = 0;

    ViewOverlayStage&    overlayStage;
    PostProcessingStage& postProcessStage;
    EntityIdPass*        entityIdPass = nullptr;
    Sprite2DStage*       spriteStage  = nullptr;
};

struct DeferredSkyboxPassParams
{
    struct BufferInput
    {
        RGBufferHandle handle{};
        RGBufferRange  range{};
    };

    BufferInput                                    frame{};
    RGTextureHandle                                viewColor{};
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

/// Scene sprites render into the lit scene color before bloom, so scene depth
/// occludes them and they are graded with everything else. Their own draw
/// policy (opaque writes depth, translucent only tests) is pipeline state.
struct DeferredSprite2DPassParams
{
    RGTextureHandle     color{};
    RGTextureHandle     depth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    Sprite2DPassBindings bindings{};
};

namespace deferred_frame_graph_passes
{

/// Import the View's host-written buffers (frame/light/skinning UBOs, optional
/// SSAO frame UBO, skybox frame UBO). One import site for every pass that reads
/// them, so the graph sees one binding per buffer.
void importFrameBuffers(DeferredFrameGraphPassContext& context);

/// Import the tail passes' owned View targets: the picking id target and the
/// display output. The composition's targets (GBuffer, SceneColor, SSAO) are
/// imported by their features from the View lease.
void createTailAttachmentTextures(DeferredFrameGraphPassContext& context);

void appendForwardOpaque(DeferredFrameGraphPassContext& context);
void appendSkybox(DeferredFrameGraphPassContext& context);
void appendSprite2D(DeferredFrameGraphPassContext& context);
void appendBloom(DeferredFrameGraphPassContext& context);
void appendForwardTransparent(DeferredFrameGraphPassContext& context);
void appendEntityId(DeferredFrameGraphPassContext& context);
void appendPostprocess(DeferredFrameGraphPassContext& context);

} // namespace deferred_frame_graph_passes

} // namespace ya
