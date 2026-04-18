#include "DeferredRenderPipeline.h"

#include "ImGuiHelper.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"
#include "Resource/TextureLibrary.h"
#include "Runtime/App/App.h"
#include "Runtime/App/ForwardRender/ShadowStage.h"
#include "Scene/SceneManager.h"


#include <algorithm>
#include <cctype>
#include <chrono>
#include <string_view>

namespace ya
{

namespace
{

struct ShadowDepthFormatOption
{
    EFormat::T       format;
    std::string_view label;
};

struct RenderTargetFormatOption
{
    EFormat::T       format;
    std::string_view label;
};

constexpr std::array<ShadowDepthFormatOption, 3> kShadowDepthFormats = {{
    {EFormat::D16_UNORM, "D16_UNORM"},
    {EFormat::D24_UNORM_S8_UINT, "D24_UNORM_S8_UINT"},
    {EFormat::D32_SFLOAT, "D32_SFLOAT"},
}};

constexpr std::array<RenderTargetFormatOption, 5> kColorFormats = {{
    {EFormat::R8_UNORM, "R8_UNORM"},
    {EFormat::R8G8_UNORM, "R8G8_UNORM"},
    {EFormat::R8G8B8A8_UNORM, "R8G8B8A8_UNORM"},
    {EFormat::R16G16B16A16_SFLOAT, "R16G16B16A16_SFLOAT"},
    {EFormat::R32_SFLOAT, "R32_SFLOAT"},
}};

struct RenderTargetEditorEntry
{
    const char*    label;
    IRenderTarget* rt;
};

bool isDepthAttachmentSelection(const IRenderTarget* rt, int attachmentIndex)
{
    if (!rt) {
        return false;
    }

    const int colorCount = static_cast<int>(rt->getColorAttachmentDescs().size());
    return rt->getDepthAttachmentDesc().has_value() && attachmentIndex == colorCount;
}

bool containsInsensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) {
        return true;
    }

    auto toLower = [](unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    };

    std::string haystackLower(haystack.begin(), haystack.end());
    std::string needleLower(needle.begin(), needle.end());
    std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(), toLower);
    std::transform(needleLower.begin(), needleLower.end(), needleLower.begin(), toLower);
    return haystackLower.find(needleLower) != std::string::npos;
}

const char* formatLabel(EFormat::T format)
{
    switch (format) {
    case EFormat::D16_UNORM:
        return "D16_UNORM";
    case EFormat::D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT";
    case EFormat::D32_SFLOAT:
        return "D32_SFLOAT";
    case EFormat::R8_UNORM:
        return "R8_UNORM";
    case EFormat::R8G8_UNORM:
        return "R8G8_UNORM";
    case EFormat::R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case EFormat::R16G16B16A16_SFLOAT:
        return "R16G16B16A16_SFLOAT";
    default:
        return "Unknown";
    }
}

IImageView* getAttachmentImageView(IRenderTarget* rt, int attachmentIndex)
{
    if (!rt) {
        return nullptr;
    }

    auto* fb = rt->getCurFrameBuffer();
    if (!fb) {
        return nullptr;
    }

    const int colorCount = static_cast<int>(rt->getColorAttachmentDescs().size());
    if (attachmentIndex >= 0 && attachmentIndex < colorCount) {
        auto* texture = fb->getColorTexture(static_cast<uint32_t>(attachmentIndex));
        return texture ? texture->getImageView() : nullptr;
    }

    if (attachmentIndex == colorCount) {
        auto* texture = fb->getDepthTexture();
        return texture ? texture->getImageView() : nullptr;
    }

    return nullptr;
}

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

    _render            = desc.render;
    _bViewportPassOpen = false;
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");

    Extent2D extent{
        .width  = static_cast<uint32_t>(desc.windowW),
        .height = static_cast<uint32_t>(desc.windowH),
    };

    initRenderTargets(extent);
    initShadowResources();

    _shadowStage = ya::makeShared<ShadowStage>();
    _shadowStage->setRenderTarget(_shadowDepthRT);
    _shadowStage->init(_render);

    // GBufferStage — owns frame/light UBOs, pipelines, material pools
    _gBufferStage = ya::makeShared<GBufferStage>();
    _gBufferStage->init(_render);

    // LightStage — borrows frame+light DS from GBufferStage, reads GBuffer textures
    _lightStage = ya::makeShared<LightStage>();
    _lightStage->setup(_gBufferStage.get(), _gBufferRT.get());
    std::array<IImageView*, MAX_POINT_LIGHTS> shadowPointCubeViews{};
    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        shadowPointCubeViews[lightIndex] = _shadowPointCubeIVs[lightIndex].get();
    }
    _lightStage->setShadowResources(_shadowDirectionalDepthIV.get(), shadowPointCubeViews, _shadowSampler.get());
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
    if (_shadowStage) {
        _shadowStage->destroy();
        _shadowStage.reset();
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
    _render = nullptr;
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
    _shadowDepthRT->flushDirty();

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
        if (_lightStage) {
            std::array<IImageView*, MAX_POINT_LIGHTS> shadowPointCubeViews{};
            for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
                shadowPointCubeViews[lightIndex] = _shadowPointCubeIVs[lightIndex].get();
            }
            _lightStage->setShadowResources(_shadowDirectionalDepthIV.get(), shadowPointCubeViews, _shadowSampler.get());
        }
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

    const uint32_t shadowedPointLightBudget = (_lightStage && _lightStage->isShadowMappingEnabled() && _lightStage->isPointLightShadowEnabled())
                                                ? _lightStage->getMaxPointLightShadowCount()
                                                : 0;
    if (_gBufferStage) {
        _gBufferStage->setMaxShadowedPointLights(shadowedPointLightBudget);
    }

    if (_shadowStage && _lightStage && _lightStage->isShadowMappingEnabled()) {
        _shadowStage->setPointLightShadowEnabled(_lightStage->isPointLightShadowEnabled());
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
    ImGui::Checkbox("Enable Perf Stats", &_bEnablePerfStats);
    ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");

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

    renderRenderTargetEditor();

    if (_gBufferStage) _gBufferStage->renderGUI();
    if (_shadowStage) _shadowStage->renderGUI();
    if (_lightStage) _lightStage->renderGUI();
    if (_overlayStage) _overlayStage->renderGUI();
    _postProcessStage.renderGUI();

    ImGui::TreePop();
}

void DeferredRenderPipeline::renderRenderTargetEditor()
{
    if (!ImGui::TreeNode("RT Editor")) {
        return;
    }

    std::array<RenderTargetEditorEntry, 3> entries = {{
        {.label = "GBuffer", .rt = _gBufferRT.get()},
        {.label = "Viewport", .rt = _viewportRT.get()},
        {.label = "Shadow", .rt = _shadowDepthRT.get()},
    }};

    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##rt-editor-search", "Search RT...", _rtEditorTargetSearch, sizeof(_rtEditorTargetSearch));

    std::vector<int> filteredIndices;
    for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
        if (containsInsensitive(entries[index].label, _rtEditorTargetSearch)) {
            filteredIndices.push_back(index);
        }
    }

    if (filteredIndices.empty()) {
        ImGui::TextUnformatted("No render target matches the current search.");
        ImGui::TreePop();
        return;
    }

    if (std::find(filteredIndices.begin(), filteredIndices.end(), _rtEditorSelectedTargetIndex) == filteredIndices.end()) {
        _rtEditorSelectedTargetIndex     = filteredIndices.front();
        _rtEditorSelectedAttachmentIndex = 0;
    }

    const auto& selectedEntry = entries[_rtEditorSelectedTargetIndex];
    if (ImGui::BeginCombo("Target", selectedEntry.label)) {
        for (int index : filteredIndices) {
            const bool bSelected = index == _rtEditorSelectedTargetIndex;
            if (ImGui::Selectable(entries[index].label, bSelected)) {
                _rtEditorSelectedTargetIndex     = index;
                _rtEditorSelectedAttachmentIndex = 0;
            }
            if (bSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (!selectedEntry.rt) {
        ImGui::TextUnformatted("Selected RT is not initialized.");
        ImGui::TreePop();
        return;
    }

    const int  colorCount            = static_cast<int>(selectedEntry.rt->getColorAttachmentDescs().size());
    const bool bHasDepth             = selectedEntry.rt->getDepthAttachmentDesc().has_value();
    const int  attachmentCount       = colorCount + (bHasDepth ? 1 : 0);
    _rtEditorSelectedAttachmentIndex = std::clamp(_rtEditorSelectedAttachmentIndex, 0, std::max(attachmentCount - 1, 0));

    const std::string selectedAttachmentLabel = _rtEditorSelectedAttachmentIndex < colorCount
                                                  ? std::format("Color[{}]", _rtEditorSelectedAttachmentIndex)
                                                  : std::string("Depth");
    if (attachmentCount > 0 && ImGui::BeginCombo("Attachment", selectedAttachmentLabel.c_str())) {
        for (int attachmentIndex = 0; attachmentIndex < attachmentCount; ++attachmentIndex) {
            const bool        bSelected = attachmentIndex == _rtEditorSelectedAttachmentIndex;
            const std::string label     = attachmentIndex < colorCount ? std::format("Color[{}]", attachmentIndex) : "Depth";
            if (ImGui::Selectable(label.c_str(), bSelected)) {
                _rtEditorSelectedAttachmentIndex = attachmentIndex;
            }
            if (bSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Text("Extent: %u x %u", selectedEntry.rt->getExtent().width, selectedEntry.rt->getExtent().height);
    ImGui::Text("Frame Buffers: %u", selectedEntry.rt->getFrameBufferCount());

    EFormat::T currentFormat = EFormat::Undefined;
    if (_rtEditorSelectedAttachmentIndex < colorCount) {
        currentFormat = selectedEntry.rt->getColorAttachmentDescs()[_rtEditorSelectedAttachmentIndex].format;
    }
    else if (bHasDepth) {
        currentFormat = selectedEntry.rt->getDepthAttachmentDesc()->format;
    }
    ImGui::Text("Format: %s", formatLabel(currentFormat));

    auto* sampler = TextureLibrary::get().getDefaultSampler().get();
    if (auto* imageView = getAttachmentImageView(selectedEntry.rt, _rtEditorSelectedAttachmentIndex)) {
        ImGuiHelper::Image(imageView, sampler, "RT Preview", ImVec2(256.0f, 256.0f));
    }

    ImGui::SeparatorText("Attachment Format");
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##rt-format-search", "Search format...", _rtEditorFormatSearch, sizeof(_rtEditorFormatSearch));

    const bool  bEditingDepth = isDepthAttachmentSelection(selectedEntry.rt, _rtEditorSelectedAttachmentIndex);
    const char* comboLabel    = bEditingDepth ? "Depth Format" : "Color Format";
    const char* currentLabel  = formatLabel(currentFormat);
    if (ImGui::BeginCombo(comboLabel, currentLabel)) {
        if (bEditingDepth) {
            for (const auto& option : kShadowDepthFormats) {
                if (!containsInsensitive(option.label, _rtEditorFormatSearch)) {
                    continue;
                }

                const bool bSelected = option.format == currentFormat;
                if (ImGui::Selectable(option.label.data(), bSelected)) {
                    if (selectedEntry.rt == _shadowDepthRT.get()) {
                        _shadowDepthFormat = option.format;
                        selectedEntry.rt->setDepthAttachmentFormat(option.format);
                    }
                    else {
                        if (_gBufferRT) {
                            _gBufferRT->setDepthAttachmentFormat(option.format);
                        }
                        if (_viewportRT) {
                            _viewportRT->setDepthAttachmentFormat(option.format);
                        }
                    }
                }
                if (bSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        else {
            for (const auto& option : kColorFormats) {
                if (!containsInsensitive(option.label, _rtEditorFormatSearch)) {
                    continue;
                }

                const bool bSelected = option.format == currentFormat;
                if (ImGui::Selectable(option.label.data(), bSelected)) {
                    selectedEntry.rt->setColorAttachmentFormat(static_cast<uint32_t>(_rtEditorSelectedAttachmentIndex), option.format);
                }
                if (bSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        ImGui::EndCombo();
    }

    if (bEditingDepth && selectedEntry.rt != _shadowDepthRT.get()) {
        ImGui::TextWrapped("GBuffer depth and Viewport depth are applied together so the depth copy path stays format-compatible on the next frame.");
    }
    else if (bEditingDepth && selectedEntry.rt == _shadowDepthRT.get()) {
        ImGui::TextWrapped("D16_UNORM usually reduces depth bandwidth and memory versus D24/D32, but actual gain depends on the GPU and driver. Test 0/1/2 point-shadow budgets with each format.");
    }

    ImGui::TreePop();
}

} // namespace ya
