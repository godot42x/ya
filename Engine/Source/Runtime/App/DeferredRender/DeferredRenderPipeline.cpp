#include "DeferredRenderPipeline.h"

#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"
#include "Runtime/App/App.h"


#include <algorithm>
#include <chrono>

namespace ya
{

namespace
{

} // namespace

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

void DeferredRenderPipeline::initShadowResources()
{
    if (!_render || _shadowDepthRT) {
        return;
    }

    _shadowDepthRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "Deferred Shadow Map RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = {.width = 512, .height = 512},
        .frameBufferCount = 1,
        .layerCount       = 1 + MAX_POINT_LIGHTS * 6,
        .attachments      = {
                 .depthAttach = AttachmentDescription{
                     .index            = 0,
                     .format           = _shadowDepthFormat,
                     .samples          = ESampleCount::Sample_1,
                     .loadOp           = EAttachmentLoadOp::Clear,
                     .storeOp          = EAttachmentStoreOp::Store,
                     .initialLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                     .finalLayout      = EImageLayout::ShaderReadOnlyOptimal,
                     .usage            = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
                     .imageCreateFlags = EImageCreateFlag::CubeCompatible,
            },
        },
    });
    YA_CORE_ASSERT(_shadowDepthRT, "Failed to create deferred shadow render target");

    rebuildShadowViews();

    _shadowSampler = Sampler::create(SamplerDesc{
        .label        = "deferred-shadow",
        .minFilter    = EFilter::Linear,
        .magFilter    = EFilter::Linear,
        .mipmapMode   = ESamplerMipmapMode::Linear,
        .addressModeU = ESamplerAddressMode::ClampToBorder,
        .addressModeV = ESamplerAddressMode::ClampToBorder,
        .addressModeW = ESamplerAddressMode::ClampToBorder,
        .borderColor  = SamplerDesc::BorderColor{.type = SamplerDesc::EBorderColor::FloatOpaqueWhite, .color = {1, 1, 1, 1}},
    });
}

void DeferredRenderPipeline::destroyShadowResources()
{
    if (_shadowStage) {
        _shadowStage->destroy();
        _shadowStage.reset();
    }

    _shadowDepthRT.reset();
    _shadowDirectionalDepthIV.reset();
    for (auto& imageView : _shadowPointCubeIVs) {
        imageView.reset();
    }
    for (auto& faceViews : _shadowPointFaceIVs) {
        for (auto& imageView : faceViews) {
            imageView.reset();
        }
    }
    _shadowSampler.reset();
}

void DeferredRenderPipeline::syncShadowSettings()
{
    if (_lightStage) {
        _lightStage->setShadowSettings(_bEnableShadowMapping, _bEnablePointLightShadow);

        std::array<IImageView*, MAX_POINT_LIGHTS> shadowPointCubeViews{};
        if (_bEnableShadowMapping && _shadowDirectionalDepthIV && _shadowSampler) {
            for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
                shadowPointCubeViews[lightIndex] = _shadowPointCubeIVs[lightIndex].get();
            }
            _lightStage->setShadowResources(_shadowDirectionalDepthIV.get(), shadowPointCubeViews, _shadowSampler.get());
        }
        else {
            _lightStage->setShadowResources(nullptr, shadowPointCubeViews, nullptr);
        }
    }

    if (_shadowStage) {
        _shadowStage->setPointLightShadowEnabled(_bEnablePointLightShadow);
        _shadowStage->setMaxPointLightShadowCount(_bEnablePointLightShadow ? _maxPointLightShadowCount : 0);
    }

    if (_gBufferStage) {
        _gBufferStage->setMaxShadowedPointLights(
            (_bEnableShadowMapping && _bEnablePointLightShadow) ? _maxPointLightShadowCount : 0);
    }
}

void DeferredRenderPipeline::queueShadowSettingsChange(bool     bEnableShadowMapping,
                                                       bool     bEnablePointLightShadow,
                                                       uint32_t maxPointLightShadowCount)
{
    _pendingEnableShadowMapping      = bEnableShadowMapping;
    _pendingEnablePointLightShadow   = bEnablePointLightShadow;
    _pendingMaxPointLightShadowCount = maxPointLightShadowCount;

    if (_bShadowSettingsChangePending) {
        return;
    }

    _bShadowSettingsChangePending = true;
    App::get()->taskManager.registerFrameTask(
        [this]()
        {
            _bShadowSettingsChangePending = false;

            const bool bToggleChanged = _bEnableShadowMapping != _pendingEnableShadowMapping ||
                                        _bEnablePointLightShadow != _pendingEnablePointLightShadow;

            _maxPointLightShadowCount = _pendingMaxPointLightShadowCount;
            if (bToggleChanged) {
                applyShadowSettings(_pendingEnableShadowMapping, _pendingEnablePointLightShadow);
                return;
            }

            syncShadowSettings();
        });
}

void DeferredRenderPipeline::applyShadowSettings(bool bEnableShadowMapping, bool bEnablePointLightShadow)
{
    const bool bShadowMappingChanged = _bEnableShadowMapping != bEnableShadowMapping;
    const bool bPointShadowChanged   = _bEnablePointLightShadow != bEnablePointLightShadow;
    if (!bShadowMappingChanged && !bPointShadowChanged) {
        return;
    }

    if (_render) {
        _render->waitIdle();
    }

    _bEnableShadowMapping    = bEnableShadowMapping;
    _bEnablePointLightShadow = bEnablePointLightShadow;

    if (_bEnableShadowMapping) {
        if (!_shadowDepthRT) {
            initShadowResources();
        }
        if (!_shadowStage && _shadowDepthRT) {
            _shadowStage = ya::makeShared<ShadowStage>();
            _shadowStage->setRenderTarget(_shadowDepthRT);
            _shadowStage->init(_render);
        }
    }
    else {
        destroyShadowResources();
    }

    syncShadowSettings();
}

void DeferredRenderPipeline::rebuildShadowViews()
{
    _shadowDirectionalDepthIV.reset();
    for (auto& imageView : _shadowPointCubeIVs) {
        imageView.reset();
    }
    for (auto& faceViews : _shadowPointFaceIVs) {
        for (auto& imageView : faceViews) {
            imageView.reset();
        }
    }

    auto* textureFactory    = _render->getTextureFactory();
    auto* shadowFrameBuffer = _shadowDepthRT->getCurFrameBuffer();
    YA_CORE_ASSERT(shadowFrameBuffer, "Deferred shadow resources require a valid framebuffer");

    auto* shadowDepthTexture = shadowFrameBuffer->getDepthTexture();
    YA_CORE_ASSERT(shadowDepthTexture, "Deferred shadow resources require a valid depth texture");

    auto shadowImage = shadowDepthTexture->getImageShared();
    YA_CORE_ASSERT(textureFactory && shadowImage, "Deferred shadow resources require a valid image");

    _shadowDirectionalDepthIV = textureFactory->createImageView(shadowImage, ImageViewCreateInfo{
                                                                                 .label          = "Deferred Shadow Directional Depth IV",
                                                                                 .viewType       = EImageViewType::View2D,
                                                                                 .aspectFlags    = EImageAspect::Depth,
                                                                                 .baseMipLevel   = 0,
                                                                                 .levelCount     = 1,
                                                                                 .baseArrayLayer = 0,
                                                                                 .layerCount     = 1,
                                                                             });

    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        _shadowPointCubeIVs[lightIndex] = textureFactory->createImageView(shadowImage, ImageViewCreateInfo{
                                                                                           .label          = std::format("Deferred Shadow Point[{}] CubeIV", lightIndex),
                                                                                           .viewType       = EImageViewType::ViewCube,
                                                                                           .aspectFlags    = EImageAspect::Depth,
                                                                                           .baseMipLevel   = 0,
                                                                                           .levelCount     = 1,
                                                                                           .baseArrayLayer = 1 + lightIndex * 6,
                                                                                           .layerCount     = 6,
                                                                                       });

        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            _shadowPointFaceIVs[lightIndex][faceIndex] = textureFactory->createImageView(shadowImage, ImageViewCreateInfo{
                                                                                                          .label          = std::format("Deferred Shadow Point[{}] Face[{}]", lightIndex, faceIndex),
                                                                                                          .viewType       = EImageViewType::View2D,
                                                                                                          .aspectFlags    = EImageAspect::Depth,
                                                                                                          .baseMipLevel   = 0,
                                                                                                          .levelCount     = 1,
                                                                                                          .baseArrayLayer = 1 + lightIndex * 6 + faceIndex,
                                                                                                          .layerCount     = 1,
                                                                                                      });
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Init / Shutdown
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::init(const InitDesc& desc)
{
    shutdown();

    _render                          = desc.render;
    _bViewportPassOpen               = false;
    _bShadowSettingsChangePending    = false;
    _pendingEnableShadowMapping      = _bEnableShadowMapping;
    _pendingEnablePointLightShadow   = _bEnablePointLightShadow;
    _pendingMaxPointLightShadowCount = _maxPointLightShadowCount;
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");

    Extent2D extent{
        .width  = static_cast<uint32_t>(desc.windowW),
        .height = static_cast<uint32_t>(desc.windowH),
    };

    initRenderTargets(extent);
    if (_bEnableShadowMapping) {
        initShadowResources();

        _shadowStage = ya::makeShared<ShadowStage>();
        _shadowStage->setRenderTarget(_shadowDepthRT);
        _shadowStage->init(_render);
    }

    // GBufferStage — owns frame/light UBOs, pipelines, material pools
    _gBufferStage = ya::makeShared<GBufferStage>();
    _gBufferStage->init(_render);

    // LightStage — borrows frame+light DS from GBufferStage, reads GBuffer textures
    _lightStage = ya::makeShared<LightStage>();
    _lightStage->setup(_gBufferStage.get(), _gBufferRT.get());
    _lightStage->init(_render);
    syncShadowSettings();

    // ViewportOverlayStage — skybox + forward overlay (SimpleMaterial debug)
    _overlayStage = ya::makeShared<ViewportOverlayStage>();
    _overlayStage->init(_render);

    // PostProcess
    _postProcessStage.init(PostProcessingStage::InitDesc{
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
    destroyShadowResources();
    _bShadowSettingsChangePending = false;
    _render                       = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Tick
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::tick(const TickDesc& desc)
{
    const auto tickStart = std::chrono::steady_clock::now();
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

    _lastPointLightCount = desc.frameData->numPointLights;
    _lastDrawCount       = static_cast<uint32_t>(desc.frameData->totalDrawCount());

    // Begin frame
    _postProcessStage.beginFrame();
    const bool bViewportDirty = _viewportRT && _viewportRT->bDirty;
    const bool bGBufferDirty  = _gBufferRT && _gBufferRT->bDirty;
    _viewportRT->flushDirty();
    _gBufferRT->flushDirty();
    const bool bShadowResourcesDirty = _shadowDepthRT && _shadowDepthRT->bDirty;
    if (_shadowDepthRT) {
        _shadowDepthRT->flushDirty();
    }

    if (bGBufferDirty) {
        _cachedAlbedoSpecImageViewHandle = nullptr;
        _debugAlbedoRGBView.reset();
        _debugSpecularAlphaView.reset();
        if (_gBufferStage) {
            _gBufferStage->refreshPipelineFormats(_gBufferRT.get());
        }
    }

    if (bViewportDirty) {
        if (_lightStage) {
            _lightStage->refreshPipelineFormats(_viewportRT.get());
        }
        if (_overlayStage) {
            _overlayStage->refreshPipelineFormats(_viewportRT.get());
        }
    }

    if (bShadowResourcesDirty) {
        rebuildShadowViews();
        if (_shadowStage) {
            _shadowStage->refreshPipelineFromRenderTarget();
        }
        syncShadowSettings();
    }

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

    const uint32_t shadowedPointLightBudget = (_bEnableShadowMapping && _bEnablePointLightShadow)
                                                ? _maxPointLightShadowCount
                                                : 0;
    if (_gBufferStage) {
        _gBufferStage->setMaxShadowedPointLights(shadowedPointLightBudget);
    }

    if (_shadowStage && _bEnableShadowMapping) {
        _shadowStage->setPointLightShadowEnabled(_bEnablePointLightShadow);
        _shadowStage->setMaxPointLightShadowCount(shadowedPointLightBudget);
        const auto shadowStart = std::chrono::steady_clock::now();
        _shadowStage->prepare(stageCtx);

        RenderingInfo shadowMapRI{
            .label           = "Deferred Shadow Map Pass",
            .renderArea      = Rect2D{.pos = {0, 0}, .extent = _shadowDepthRT->getExtent().toVec2()},
            .depthClearValue = ClearValue(1.0f, 0),
            .renderTarget    = _shadowDepthRT.get(),
        };
        desc.cmdBuf->beginRendering(shadowMapRI);
        _shadowStage->execute(stageCtx);
        desc.cmdBuf->endRendering(shadowMapRI);
        _lastShadowCpuMs = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shadowStart).count());
    }
    else {
        _lastShadowCpuMs = 0.0f;
    }

    // ── GBuffer Pass ─────────────────────────────────────────────
    const auto gbufferStart = std::chrono::steady_clock::now();
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
    _lastGBufferCpuMs = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gbufferStart).count());

    const auto depthCopyStart = std::chrono::steady_clock::now();
    copyGBufferDepthToViewport(desc.cmdBuf);
    _lastDepthCopyCpuMs = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - depthCopyStart).count());

    // ── Viewport Pass (Light + Skybox + Overlay) ─────────────────
    beginViewportRendering(desc);

    const auto lightStart = std::chrono::steady_clock::now();
    _lightStage->prepare(stageCtx);
    _lightStage->execute(stageCtx);
    _lastLightCpuMs = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lightStart).count());

    const auto overlayStart = std::chrono::steady_clock::now();
    _overlayStage->prepare(stageCtx);
    _overlayStage->execute(stageCtx);
    _lastOverlayCpuMs = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - overlayStart).count());

    // Viewport pass left open for App-level 2D rendering
    _lastTickCpuMs = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tickStart).count());
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
        cmdBuf, inputTexture, _lastTickDesc.viewportRect.extent, &_lastTickCtx);
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
    ImGui::Checkbox("Enable Perf Stats", &_bEnablePerfStats);
    ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");

    bool bEnableShadowMapping     = _bShadowSettingsChangePending ? _pendingEnableShadowMapping : _bEnableShadowMapping;
    bool bEnablePointLightShadow  = _bShadowSettingsChangePending ? _pendingEnablePointLightShadow : _bEnablePointLightShadow;
    int  maxPointLightShadowCount = static_cast<int>(_bShadowSettingsChangePending ? _pendingMaxPointLightShadowCount : _maxPointLightShadowCount);
    bool bShadowSettingsDirty     = false;

    bShadowSettingsDirty |= ImGui::Checkbox("Enable Shadow Mapping", &bEnableShadowMapping);
    bShadowSettingsDirty |= ImGui::Checkbox("Enable Point Light Shadow", &bEnablePointLightShadow);
    bShadowSettingsDirty |= ImGui::SliderInt("Max Point Light Shadows", &maxPointLightShadowCount, 0, MAX_POINT_LIGHTS);

    if (bShadowSettingsDirty) {
        queueShadowSettingsChange(
            bEnableShadowMapping,
            bEnablePointLightShadow,
            static_cast<uint32_t>(std::clamp(maxPointLightShadowCount, 0, static_cast<int>(MAX_POINT_LIGHTS))));
    }

    if (_bEnablePerfStats) {
        if (ImGui::TreeNode("Perf"))
        {
            ImGui::Text("CPU tick: %.3f ms", _lastTickCpuMs);
            ImGui::Text("CPU shadow: %.3f ms", _lastShadowCpuMs);
            ImGui::Text("CPU gbuffer: %.3f ms", _lastGBufferCpuMs);
            ImGui::Text("CPU depth copy: %.3f ms", _lastDepthCopyCpuMs);
            ImGui::Text("CPU light: %.3f ms", _lastLightCpuMs);
            ImGui::Text("CPU overlay: %.3f ms", _lastOverlayCpuMs);
            ImGui::Text("Draw items: %u", _lastDrawCount);
            ImGui::Text("Point lights: %u", _lastPointLightCount);
            ImGui::TreePop();
        }
    }

    if (_shadowStage) _shadowStage->renderGUI();
    if (_gBufferStage) _gBufferStage->renderGUI();
    if (_lightStage) _lightStage->renderGUI();
    if (_overlayStage) _overlayStage->renderGUI();
    _postProcessStage.renderGUI();

    ImGui::TreePop();
}

} // namespace ya
