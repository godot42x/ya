#include "DeferredRenderPipeline.h"

#include "Runtime/App/App.h"
#include "Scene/SceneManager.h"

namespace ya
{

DeferredRenderPipeline::~DeferredRenderPipeline()
{
    shutdown();
}

void DeferredRenderPipeline::initRenderTargets(Extent2D extent)
{
    _gBufferRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "GBuffer RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = extent,
        .frameBufferCount = 1,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index         = 0,
                    .format        = SIGNED_LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 1,
                    .format        = SIGNED_LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 2,
                    .format        = LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 3,
                    .format        = SHADING_MODEL_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index          = 4,
                .format         = DEPTH_FORMAT,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::Clear,
                .stencilStoreOp = EAttachmentStoreOp::Store,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
            },
        },
    });

    _viewportRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "Deferred Viewport RT",
        .bSwapChainTarget = false,
        .extent           = extent,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index          = 0,
                    .format         = LINEAR_FORMAT,
                    .samples        = ESampleCount::Sample_1,
                    .loadOp         = EAttachmentLoadOp::Clear,
                    .storeOp        = EAttachmentStoreOp::Store,
                    .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp = EAttachmentStoreOp::DontCare,
                    .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                    .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index          = 1,
                .format         = DEPTH_FORMAT,
                .samples        = ESampleCount::Sample_1,
                .loadOp         = EAttachmentLoadOp::Load,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled | EImageUsage::TransferDst,
            },
        },
    });
}

// ═══════════════════════════════════════════════════════════════════════
// Init / Shutdown
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::init(const InitDesc& desc)
{
    shutdown();

    _render            = desc.render;
    _bViewportPassOpen = false;
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");

    Extent2D extent{
        .width  = static_cast<uint32_t>(desc.windowW),
        .height = static_cast<uint32_t>(desc.windowH),
    };

    initRenderTargets(extent);

    // GBufferStage — owns frame/light UBOs, pipelines, material pools
    _gBufferStage = ya::makeShared<GBufferStage>();
    _gBufferStage->init(_render);

    // LightStage — borrows frame+light DS from GBufferStage, reads GBuffer textures
    _lightStage = ya::makeShared<LightStage>();
    _lightStage->setup(_gBufferStage.get(), _gBufferRT.get());
    _lightStage->init(_render);

    // ViewportOverlayStage — skybox + forward overlay (SimpleMaterial debug)
    _overlayStage = ya::makeShared<ViewportOverlayStage>();
    _overlayStage->init(_render);

    // PostProcess
    _postProcessStage.init(PostProcessStage::InitDesc{
        .render      = _render,
        .colorFormat = LINEAR_FORMAT,
        .width       = extent.width,
        .height      = extent.height,
    });

    _render->waitIdle();
}

void DeferredRenderPipeline::shutdown()
{
    _bViewportPassOpen = false;
    _postProcessStage.shutdown();
    viewportTexture = nullptr;

    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _cachedAlbedoSpecImageViewHandle = nullptr;

    if (_overlayStage) {
        _overlayStage->destroy();
        _overlayStage.reset();
    }
    if (_lightStage) {
        _lightStage->destroy();
        _lightStage.reset();
    }
    if (_gBufferStage) {
        _gBufferStage->destroy();
        _gBufferStage.reset();
    }

    _viewportRT.reset();
    _gBufferRT.reset();
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Tick
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::tick(const TickDesc& desc)
{
    YA_CORE_ASSERT(desc.cmdBuf, "DeferredRenderPipeline requires a command buffer");
    desc.cmdBuf->debugBeginLabel("Deferred Pipeline");

    if (desc.viewportRect.extent.x <= 0 || desc.viewportRect.extent.y <= 0) {
        desc.cmdBuf->debugEndLabel();
        return;
    }

    if (!desc.frameData) {
        desc.cmdBuf->debugEndLabel();
        return;
    }

    // Begin frame
    _postProcessStage.beginFrame();
    _viewportRT->flushDirty();
    _gBufferRT->flushDirty();

    const uint32_t vpW = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH = static_cast<uint32_t>(desc.viewportRect.extent.y);

    // Build stage context
    // NOTE: flightIndex is 0 for now (flightFrameSize=1); will come from VulkanRender when changed to 2.
    RenderStageContext stageCtx{
        .cmdBuf         = desc.cmdBuf,
        .frameData      = desc.frameData,
        .flightIndex    = 0,
        .deltaTime      = desc.dt,
        .viewportExtent = {.width = vpW, .height = vpH},
    };

    // ── GBuffer Pass ─────────────────────────────────────────────
    _gBufferStage->prepare(stageCtx);

    {
        RenderingInfo gBufferRI{
            .label            = "GBuffer Pass",
            .renderArea       = Rect2D{.pos = {0, 0}, .extent = _gBufferRT->getExtent().toVec2()},
            .layerCount       = 1,
            .colorClearValues = {
                ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
            },
            .depthClearValue = ClearValue(1.0f, 0),
            .renderTarget    = _gBufferRT.get(),
        };
        desc.cmdBuf->beginRendering(gBufferRI);

        float gbVpY = 0.0f;
        float gbVpH = static_cast<float>(vpH);
        if (_bReverseViewportY) {
            gbVpY = static_cast<float>(vpH);
            gbVpH = -gbVpH;
        }
        desc.cmdBuf->setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
        desc.cmdBuf->setScissor(0, 0, vpW, vpH);

        _gBufferStage->execute(stageCtx);

        desc.cmdBuf->endRendering(gBufferRI);
    }

    copyGBufferDepthToViewport(desc.cmdBuf);

    // ── Viewport Pass (Light + Skybox + Overlay) ─────────────────
    beginViewportRendering(desc);

    _lightStage->prepare(stageCtx);
    _lightStage->execute(stageCtx);

    _overlayStage->prepare(stageCtx);
    _overlayStage->execute(stageCtx);

    // Viewport pass left open for App-level 2D rendering
    desc.cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// Depth Copy
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::copyGBufferDepthToViewport(ICommandBuffer* cmdBuf)
{
    auto* gbufferDepth  = _gBufferRT ? _gBufferRT->getCurFrameBuffer()->getDepthTexture() : nullptr;
    auto* viewportDepth = _viewportRT ? _viewportRT->getCurFrameBuffer()->getDepthTexture() : nullptr;
    if (!cmdBuf || !gbufferDepth || !viewportDepth) {
        return;
    }

    auto* srcImage = gbufferDepth->getImage();
    auto* dstImage = viewportDepth->getImage();
    if (!srcImage || !dstImage) {
        return;
    }

    ImageSubresourceRange depthRange{
        .aspectMask     = EImageAspect::Depth,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    cmdBuf->debugBeginLabel("Copy GBuffer Depth → Viewport");

    cmdBuf->transitionImageLayoutAuto(srcImage, EImageLayout::TransferSrc, &depthRange);
    cmdBuf->transitionImageLayoutAuto(dstImage, EImageLayout::TransferDst, &depthRange);

    cmdBuf->copyImage(
        srcImage,
        EImageLayout::TransferSrc,
        dstImage,
        EImageLayout::TransferDst,
        {
            ImageCopy{
                .srcSubresource = ImageSubresourceLayers{
                    .aspectMask     = EImageAspect::Depth,
                    .mipLevel       = 0,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
                .dstSubresource = ImageSubresourceLayers{
                    .aspectMask     = EImageAspect::Depth,
                    .mipLevel       = 0,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
                .extentWidth  = _gBufferRT->getExtent().width,
                .extentHeight = _gBufferRT->getExtent().height,
                .extentDepth  = 1,
            },
        });

    cmdBuf->transitionImageLayoutAuto(srcImage, EImageLayout::ShaderReadOnlyOptimal, &depthRange);
    cmdBuf->transitionImageLayoutAuto(dstImage, EImageLayout::ShaderReadOnlyOptimal, &depthRange);
    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// Viewport Pass
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::beginViewportRendering(const TickDesc& desc)
{
    auto cmdBuf = desc.cmdBuf;

    _viewportDepthSpec = RenderingInfo::ImageSpec{
        .texture       = _viewportRT->getCurFrameBuffer()->getDepthTexture(),
        .loadOp        = EAttachmentLoadOp::Load,
        .storeOp       = EAttachmentStoreOp::Store,
        .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
        .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
    };

    _viewportRI = RenderingInfo{
        .label            = "Viewport Pass",
        .renderArea       = {.pos = {0, 0}, .extent = _viewportRT->getExtent().toVec2()},
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 0.0f)},
        .renderTarget     = _viewportRT.get(),
    };

    cmdBuf->beginRendering(_viewportRI);
    _bViewportPassOpen = true;

    const uint32_t vpW = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH = static_cast<uint32_t>(desc.viewportRect.extent.y);

    _lastTickCtx = {
        .view       = desc.view,
        .projection = desc.projection,
        .cameraPos  = desc.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _lastTickDesc = desc;
}

void DeferredRenderPipeline::endViewportPass(ICommandBuffer* cmdBuf)
{
    if (!_bViewportPassOpen) {
        return;
    }

    cmdBuf->endRendering(_viewportRI);
    _bViewportPassOpen = false;

    auto* inputTexture = _viewportRT->getCurFrameBuffer()->getColorTexture(0);
    viewportTexture    = _postProcessStage.execute(
        cmdBuf, inputTexture, _lastTickDesc.viewportRect.extent, _lastTickDesc.dt, &_lastTickCtx);
}

void DeferredRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };

    if (_gBufferRT) _gBufferRT->setExtent(newExtent);
    if (_viewportRT) _viewportRT->setExtent(newExtent);

    _cachedAlbedoSpecImageViewHandle = nullptr;
    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _postProcessStage.onViewportResized(newExtent);
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::TreeNode("Deferred Pipeline")) return;

    ImGui::Checkbox("GBuffer Reverse Viewport Y", &_bReverseViewportY);
    ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");

    if (_gBufferStage) _gBufferStage->renderGUI();
    if (_lightStage) _lightStage->renderGUI();
    if (_overlayStage) _overlayStage->renderGUI();
    _postProcessStage.renderGUI();

    ImGui::TreePop();
}

} // namespace ya
