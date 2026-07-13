#include "DeferredRenderPipeline.h"

#include "Config/ConfigManager.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "DeferredViewportResources.h"
#include "DeferredAttachmentFormats.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/RenderImage.h"
#include "Render/Core/Texture.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Runtime/App/App.h"
#include "Runtime/App/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Render/Core/RenderGraphExecutor.h"
#include <algorithm>
#include <chrono>
#include <format>

namespace ya
{

namespace
{

constexpr const char* DEFERRED_PIPELINE_CONFIG_DOC_NAME                       = "editor";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SSAO                = "render.deferred.ssao.enabled";

DeferredGBufferResources buildDeferredGBufferResources(IRenderTarget* gBufferRT)
{
    DeferredGBufferResources resources{};
    if (!gBufferRT) {
        return resources;
    }

    auto* frameBuffer = gBufferRT->getCurFrameBuffer();
    if (!frameBuffer) {
        return resources;
    }

    for (uint32_t attachmentIndex = 0; attachmentIndex < resources.color.size(); ++attachmentIndex) {
        resources.color[attachmentIndex] = frameBuffer->getColorTexture(attachmentIndex);
    }
    resources.depth = frameBuffer->getDepthTexture();
    return resources;
}

DeferredViewportResources buildDeferredViewportResources(IRenderTarget* viewportRT)
{
    DeferredViewportResources resources{};
    if (!viewportRT) {
        return resources;
    }

    auto* frameBuffer = viewportRT->getCurFrameBuffer();
    if (!frameBuffer) {
        return resources;
    }

    resources.color = frameBuffer->getColorTexture(0);
    resources.depth = frameBuffer->getDepthTexture();
    return resources;
}

DeferredAttachmentFormats buildDeferredAttachmentFormats(const IRenderTarget* renderTarget)
{
    DeferredAttachmentFormats formats{};
    if (!renderTarget) {
        return formats;
    }

    const auto& colorDescs = renderTarget->getColorAttachmentDescs();
    formats.colorFormats.reserve(colorDescs.size());
    for (const auto& desc : colorDescs) {
        formats.colorFormats.push_back(desc.format);
    }

    if (const auto depthDesc = renderTarget->getDepthAttachmentDesc(); depthDesc.has_value()) {
        formats.depthFormat = depthDesc->format;
    }

    return formats;
}

RGImportedTextureDesc makeDeferredImportedTextureDesc(Texture& texture, std::string_view label, EImageLayout::T finalLayout)
{
    YA_CORE_ASSERT(texture.getImageShared() != nullptr, "Deferred graph import requires a backing image");
    IImage* image = texture.getImage();
    YA_CORE_ASSERT(image != nullptr, "Deferred graph import requires a valid image");

    return RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = std::string(label),
            .format      = texture.getFormat(),
            .extent      = Extent3D{texture.getWidth(), texture.getHeight(), 1},
            .mipLevels   = image->getMipLevels(),
            .arrayLayers = image->getArrayLayers(),
            .usage       = image->getUsage(),
        },
        .importDesc = ImportedImageDesc{
            .label         = std::string(label),
            .nativeHandle  = static_cast<void*>(image->getHandle()),
            .format        = texture.getFormat(),
            .usage         = image->getUsage(),
            .extent        = Extent3D{texture.getWidth(), texture.getHeight(), 1},
            .mipLevels     = image->getMipLevels(),
            .arrayLayers   = image->getArrayLayers(),
            .initialLayout = image->getCompatibilityLayout(),
            .finalLayout   = finalLayout,
        },
        .image = texture.getImageShared(),
    };
}

stdptr<RenderImage> createSSAOImage(IRender* render, Extent2D extent)
{
    return createRenderImage(
        *render->getResourceFactory(),
        RenderImageDesc{
            .image = ImageCreateInfo{
                .label         = "DeferredSSAO",
                .format        = SSAOStage::AO_FORMAT,
                .extent        = {.width = extent.width, .height = extent.height, .depth = 1},
                .mipLevels     = 1,
                .arrayLayers   = 1,
                .samples       = ESampleCount::Sample_1,
                .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .initialLayout = EImageLayout::Undefined,
            },
            .defaultView = ImageViewCreateInfo{
                .label       = "DeferredSSAO_DefaultView",
                .aspectFlags = EImageAspect::Color,
            },
        });
}

void drawPerfLeaf(const char* label, float value, float parentValue = 0.0f)
{
    constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
    if (parentValue > 0.0f) {
        ImGui::TreeNodeEx(label, flags, "%s  %.3f ms  %.1f%%", label, value, value * 100.0f / parentValue);
        return;
    }
    ImGui::TreeNodeEx(label, flags, "%s  %.3f ms", label, value);
}

template <typename Fn>
void drawPerfNode(const char* label, float value, Fn&& drawChildren)
{
    if (ImGui::TreeNode(label, "%s  %.3f ms", label, value)) {
        drawChildren();
        ImGui::TreePop();
    }
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
                         .format         = VIEWPORT_COLOR_FORMAT,
                         .samples        = ESampleCount::Sample_1,
                         .loadOp         = EAttachmentLoadOp::Clear,
                         .storeOp        = EAttachmentStoreOp::Store,
                         .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                         .stencilStoreOp = EAttachmentStoreOp::DontCare,
                         .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                         .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                         .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
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
    if (!_render || _shadowResources.renderTarget) {
        return;
    }

    const auto  shadowSettings      = currentShadowSettings();
    const uint32_t shadowResolution = std::max(shadowSettings.resolution, 1u);

    _shadowResources.init(_render, ShadowMapResourceDesc{
        .renderTargetLabel = "Deferred Shadow Map RenderTarget",
        .samplerLabel      = "deferred-shadow",
        .viewLabelPrefix   = "Deferred Shadow",
        .extent            = {.width = shadowResolution, .height = shadowResolution},
        .depthFormat       = _shadowDepthFormat,
    });
}

void DeferredRenderPipeline::destroyShadowResources()
{
    if (_shadowStage) {
        _shadowStage->destroy();
        _shadowStage.reset();
    }

    _shadowResources.destroy();
}

void DeferredRenderPipeline::syncShadowSettings()
{
    const auto shadowState = buildShadowState();

    if (_lightStage) {
        _lightStage->applyShadowState(shadowState);
    }

    if (_gBufferStage) {
        _gBufferStage->applyShadowState(shadowState);
    }
}

ShadowSettings DeferredRenderPipeline::currentShadowSettings() const
{
    return _frameShadowSettings;
}

bool DeferredRenderPipeline::isShadowMappingEnabled() const
{
    return currentShadowSettings().isEnabled();
}

ShadowRuntimeState DeferredRenderPipeline::buildShadowState() const
{
    ShadowRuntimeState shadowState{};
    const ShadowSettings shadowSettings = currentShadowSettings();
    shadowState.bEnableShadowMapping    = shadowSettings.isEnabled();
    shadowState.bEnablePointLightShadow = shadowSettings.pointLightEnabled;
    shadowState.maxShadowedPointLights  = shadowSettings.getEffectivePointLightCount();
    shadowState.shadowMapResolution     = _shadowResources.extent.width > 0 ? _shadowResources.extent.width : std::max(shadowSettings.resolution, 1u);
    shadowState.filter                  = shadowSettings.filter;
    shadowState.bias                    = shadowSettings.bias;
    shadowState.normalBias              = shadowSettings.normalBias;

    if (shadowState.bEnableShadowMapping && _shadowResources.directionalDepthIV && _shadowResources.sampler) {
        shadowState.directionalDepthIV = _shadowResources.directionalDepthIV.get();
        shadowState.sampler            = _shadowResources.sampler.get();
        for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
            shadowState.pointCubeDepthIVs[lightIndex] = _shadowResources.pointCubeIVs[lightIndex].get();
        }
    }

    return shadowState;
}

void DeferredRenderPipeline::queueShadowSettingsChange(const ShadowSettings& shadowSettings)
{
    _pendingShadowSettings = shadowSettings;
    shadow_settings::saveEditorSettings(_pendingShadowSettings);

    if (_bShadowSettingsChangePending || !_queueFrameTask) {
        if (!_queueFrameTask) {
            applyShadowSettings(_pendingShadowSettings);
        }
        return;
    }

    _bShadowSettingsChangePending = true;
    _queueFrameTask([this]()
                    {
                        _bShadowSettingsChangePending = false;

                        applyShadowSettings(_pendingShadowSettings);
                    });
}

void DeferredRenderPipeline::applyShadowSettings(const ShadowSettings& shadowSettings)
{
    const bool bWasEnabled    = currentShadowSettings().isEnabled();
    const bool bWillEnable    = shadowSettings.isEnabled();
    const bool bToggleChanged = bWasEnabled != bWillEnable;

    _frameShadowSettings = shadowSettings;
    if (_shadowSettings) {
        *_shadowSettings = shadowSettings;
    }

    if (bToggleChanged || (shadowSettings.isEnabled() && !_shadowResources.renderTarget)) {
        requestShadowResourceRefresh();
    }

    syncShadowSettings();
    shadow_settings::saveEditorSettings(shadowSettings);
}

void DeferredRenderPipeline::loadPersistentSettings()
{
    auto& cfgManager = ConfigManager::get();
    _bEnableSSAO                    = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                          DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SSAO,
                                          _bEnableSSAO);
    const ShadowSettings baselineShadowSettings = _shadowSettings ? *_shadowSettings : currentShadowSettings();
    ShadowSettings shadowSettings = shadow_settings::loadEditorSettings(baselineShadowSettings, _automationShadowOverrides);
    if (_shadowSettings) {
        *_shadowSettings = shadowSettings;
    }
    _pendingShadowSettings = shadowSettings;
}

void DeferredRenderPipeline::saveShadowSettingsToConfig(const ShadowSettings& shadowSettings) const
{
    shadow_settings::saveEditorSettings(shadowSettings);
}

void DeferredRenderPipeline::requestViewportResize(Extent2D extent)
{
    if (extent.width == 0 || extent.height == 0) {
        return;
    }

    _pendingViewportExtent  = extent;
    _bViewportResizePending = true;
}

void DeferredRenderPipeline::applyPendingViewportResize()
{
    if (!_bViewportResizePending) {
        return;
    }

    if (_gBufferRT) {
        _gBufferRT->setExtent(_pendingViewportExtent);
    }
    if (_viewportRT) {
        _viewportRT->setExtent(_pendingViewportExtent);
    }
    if (_gBufferRT) {
        _gBufferRT->flushDirty();
    }
    if (_viewportRT) {
        _viewportRT->flushDirty();
    }

    _ssaoTexture = createSSAOImage(_render, _pendingViewportExtent);
    refreshCurrentFrameResources();
    _postProcessStage.onViewportResized(_pendingViewportExtent);
    refreshViewportSizedStageResources();
    _bViewportResizePending = false;
}

void DeferredRenderPipeline::requestShadowResourceRefresh()
{
    _bShadowResourceRefreshPending = true;
}

void DeferredRenderPipeline::applyPendingShadowResourceRefresh()
{
    if (!_bShadowResourceRefreshPending || !_render) {
        return;
    }

    const auto shadowSettings = currentShadowSettings();
    _render->waitIdle();

    destroyShadowResources();

    if (shadowSettings.isEnabled()) {
        initShadowResources();
        if (!_shadowStage && _shadowResources.renderTarget) {
            _shadowStage = ya::makeShared<ShadowStage>();
            _shadowStage->setRenderTarget(_shadowResources.renderTarget);
            _shadowStage->init(_render);
        }
        if (_shadowResources.renderTarget) {
            _shadowResources.renderTarget->flushDirty();
        }
        if (_shadowStage && _shadowResources.depthImage) {
            _shadowStage->refreshShadowResources(_shadowResources.depthImage, _shadowDepthFormat, _shadowResources.extent);
        }
    }

    _bShadowResourceRefreshPending = false;
    syncShadowSettings();
}

// ═══════════════════════════════════════════════════════════════════════
// Init / Shutdown
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::init(const InitDesc& desc)
{
    shutdown();

    initPipelineState(desc);
    initStages();

    _render->waitIdle();
}

void DeferredRenderPipeline::initPipelineState(const InitDesc& desc)
{
    _render                       = desc.render;
    _shadowSettings               = desc.shadowSettings;
    _automationShadowOverrides    = desc.automationShadowOverrides;
    _queueFrameTask               = desc.queueFrameTask;
    _environmentLightingDSL       = desc.environmentLightingDSL;
    _getSceneEnvironmentLightingDescriptorSet = desc.getSceneEnvironmentLightingDescriptorSet;
    _getSceneSkyboxDescriptorSet  = desc.getSceneSkyboxDescriptorSet;
    _getDebugRenderSystem         = desc.getDebugRenderSystem;
    _getActiveScene               = desc.getActiveScene;
    _getResourceResolveSystem     = desc.getResourceResolveSystem;
    _bShadowSettingsChangePending = false;
    _bShadowResourceRefreshPending = false;
    if (_shadowSettings) {
        _frameShadowSettings = *_shadowSettings;
    }
    loadPersistentSettings();
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");

    Extent2D extent{
        .width  = static_cast<uint32_t>(desc.windowW),
        .height = static_cast<uint32_t>(desc.windowH),
    };

    initRenderTargets(extent);
    refreshCurrentFrameResources();
    _ssaoTexture = createSSAOImage(_render, extent);
    if (currentShadowSettings().isEnabled()) {
        initShadowResources();
    }

    _postProcessStage.init(PostProcessingStage::InitDesc{
        .render      = _render,
        .colorFormat = POSTPROCESS_COLOR_FORMAT,
        .width       = extent.width,
        .height      = extent.height,
    });
}

void DeferredRenderPipeline::initStages()
{
    if (_shadowResources.renderTarget) {
        _shadowStage = ya::makeShared<ShadowStage>();
        _shadowStage->setRenderTarget(_shadowResources.renderTarget);
        _shadowStage->init(_render);
        if (_shadowResources.depthImage) {
            _shadowStage->refreshShadowResources(_shadowResources.depthImage, _shadowDepthFormat, _shadowResources.extent);
        }
    }

    _gBufferStage = ya::makeShared<GBufferStage>();
    _gBufferStage->init(_render);

    _ssaoStage = ya::makeShared<SSAOStage>();
    _ssaoStage->setup(_currentGBufferResources, _ssaoTexture.get());
    _ssaoStage->setSettings(_ssaoStage->getRadius(), _ssaoStage->getBias(), _ssaoStage->getPower(), _ssaoStage->getIntensity(), _bReverseViewportY);
    _ssaoStage->init(_render);

    _lightStage = ya::makeShared<LightStage>();
    _lightStage->setup(_gBufferStage.get(), _currentGBufferResources);
    _lightStage->setEnvironmentLightingInput(LightStage::EnvironmentLightingInput{
        .environmentLightingDSL = _environmentLightingDSL,
        .getSceneEnvironmentLightingDescriptorSet = _getSceneEnvironmentLightingDescriptorSet,
    });
    _lightStage->setSSAOTexture(_ssaoTexture.get());
    _lightStage->init(_render);
    syncShadowSettings();

    _overlayStage = ya::makeShared<ViewportOverlayStage>();
    _overlayStage->setServices(ViewportOverlayStage::Services{
        .getSceneSkyboxDescriptorSet = _getSceneSkyboxDescriptorSet,
        .getDebugRenderSystem = _getDebugRenderSystem,
        .getActiveScene = _getActiveScene,
        .getResourceResolveSystem = _getResourceResolveSystem,
    });
    _overlayStage->init(_render);
}

void DeferredRenderPipeline::shutdown()
{
    _postProcessStage.shutdown();
    viewportTexture = nullptr;

    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _cachedAlbedoSpecImageViewHandle = nullptr;
    _pendingViewportExtent           = {};
    _bViewportResizePending          = false;
    _bShadowResourceRefreshPending   = false;
    _currentGBufferResources         = {};
    _currentViewportResources        = {};
    _currentGBufferFormats           = {};
    _currentViewportFormats          = {};

    if (_overlayStage) {
        _overlayStage->destroy();
        _overlayStage.reset();
    }
    if (_lightStage) {
        _lightStage->destroy();
        _lightStage.reset();
    }
    if (_ssaoStage) {
        _ssaoStage->destroy();
        _ssaoStage.reset();
    }
    if (_gBufferStage) {
        _gBufferStage->destroy();
        _gBufferStage.reset();
    }

    _ssaoTexture.reset();
    _viewportRT.reset();
    _gBufferRT.reset();
    destroyShadowResources();
    _bShadowSettingsChangePending = false;
    _environmentLightingDSL.reset();
    _getSceneEnvironmentLightingDescriptorSet = {};
    _getSceneSkyboxDescriptorSet = {};
    _getDebugRenderSystem = {};
    _getActiveScene = {};
    _getResourceResolveSystem = {};
    _render                       = nullptr;
}


// ═══════════════════════════════════════════════════════════════════════
// Tick
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::tick(const RenderPipelineFrameContext& frame)
{
    frame.cmdBuf->debugBeginLabel("Deferred Pipeline");

    if (shouldSkipTick(frame)) {
        return;
    }

    YA_PERF_SCOPE(perf::sample::deferredTick(), perf::metric::cpuTimeMs(), perf::domain::render());

    RenderStageContext stageCtx{};
    uint32_t           vpW = 0;
    uint32_t           vpH = 0;
    beginTick(frame, stageCtx, vpW, vpH);
    refreshDirtyResources();
    syncFrameSettings(frame);
    executeShadowPass(stageCtx);
    executeGBufferPass(frame, stageCtx, vpW, vpH);
    executeSSAOPass(stageCtx);
    executeDepthCopyPass(frame.cmdBuf);
    executeViewportPass(frame, stageCtx);

    frame.cmdBuf->debugEndLabel();
}

bool DeferredRenderPipeline::shouldSkipTick(const RenderPipelineFrameContext& frame) const
{
    YA_CORE_ASSERT(frame.cmdBuf, "DeferredRenderPipeline requires a command buffer");

    if (frame.viewportRect.extent.x <= 0 || frame.viewportRect.extent.y <= 0) {
        frame.cmdBuf->debugEndLabel();
        return true;
    }

    if (!frame.frameData) {
        frame.cmdBuf->debugEndLabel();
        return true;
    }

    return false;
}

void DeferredRenderPipeline::beginTick(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx, uint32_t& vpW, uint32_t& vpH)
{
    applyPendingViewportResize();
    applyPendingShadowResourceRefresh();
    _postProcessStage.beginFrame();
    captureShadowSettings(frame);
    refreshCurrentFrameResources();

    vpW = static_cast<uint32_t>(frame.viewportRect.extent.x);
    vpH = static_cast<uint32_t>(frame.viewportRect.extent.y);

    _lastPointLightCount = frame.frameData->numPointLights;
    _lastDrawCount       = static_cast<uint32_t>(frame.frameData->totalDrawCount());

    stageCtx = RenderStageContext{
        .cmdBuf         = frame.cmdBuf,
        .frameData      = frame.frameData,
        .flightIndex    = frame.flightIndex,
        .deltaTime      = frame.deltaTime,
        .viewportExtent = {.width = vpW, .height = vpH},
    };
}

void DeferredRenderPipeline::captureShadowSettings(const RenderPipelineFrameContext& frame)
{
    if (frame.shadowSettings) {
        _frameShadowSettings = *frame.shadowSettings;
    }
    else if (_shadowSettings) {
        _frameShadowSettings = *_shadowSettings;
    }
}

void DeferredRenderPipeline::updateStageFrameInputs()
{
    Scene* activeScene = _getActiveScene ? _getActiveScene() : nullptr;

    if (_lightStage) {
        _lightStage->setFrameInputs(LightStage::FrameInputs{
            .environmentLightingDescriptorSet = (_getSceneEnvironmentLightingDescriptorSet && activeScene)
                ? _getSceneEnvironmentLightingDescriptorSet(activeScene)
                : DescriptorSetHandle{},
        });
    }

    if (_overlayStage) {
        ViewportOverlayStage::FrameInputs frameInputs{};
        frameInputs.activeScene = activeScene;
        frameInputs.resourceResolveSystem = _getResourceResolveSystem ? _getResourceResolveSystem() : nullptr;

        if (frameInputs.activeScene && frameInputs.resourceResolveSystem && _getSceneSkyboxDescriptorSet) {
            const auto* skyboxState = frameInputs.resourceResolveSystem->findFirstSceneSkyboxState(frameInputs.activeScene);
            if (skyboxState && skyboxState->hasRenderableCubemap()) {
                frameInputs.skybox.descriptorSet = _getSceneSkyboxDescriptorSet(frameInputs.activeScene);
                frameInputs.skybox.mesh          = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
                for (const auto& [entity, sc, mc] : frameInputs.activeScene->getRegistry().view<SkyboxComponent, StaticMeshComponent>().each()) {
                    if (mc.isResolved() && mc.getMesh()) {
                        frameInputs.skybox.mesh = mc.getMesh();
                    }
                    break;
                }
                frameInputs.skybox.bAvailable = frameInputs.skybox.descriptorSet && frameInputs.skybox.mesh;
            }
        }

        _overlayStage->setFrameInputs(std::move(frameInputs));
    }
}

void DeferredRenderPipeline::refreshDirtyResources()
{
    const bool bViewportPipelineDirty = _viewportRT && _viewportRT->hasDirtyReason(ERenderTargetDirtyReason::Attachments);
    const bool bGBufferPipelineDirty  = _gBufferRT && _gBufferRT->hasDirtyReason(ERenderTargetDirtyReason::Attachments);

    if (!bViewportPipelineDirty && !bGBufferPipelineDirty) {
        return;
    }

    if (bGBufferPipelineDirty) {
        _gBufferRT->flushDirty();
        invalidateGBufferDependentViews();
        if (_gBufferStage) {
            _currentGBufferFormats = buildDeferredAttachmentFormats(_gBufferRT.get());
            _gBufferStage->refreshPipelineFormats(_currentGBufferFormats);
        }
    }

    if (bViewportPipelineDirty) {
        _viewportRT->flushDirty();
        _currentViewportFormats = buildDeferredAttachmentFormats(_viewportRT.get());
        if (_lightStage) {
            _lightStage->refreshPipelineFormats(_currentViewportFormats);
        }
        if (_overlayStage) {
            _overlayStage->refreshPipelineFormats(_currentViewportFormats);
        }
    }
}

void DeferredRenderPipeline::refreshViewportSizedStageResources()
{
    invalidateGBufferDependentViews();

    if (_ssaoStage) {
        _ssaoStage->setup(_currentGBufferResources, _ssaoTexture.get());
        _ssaoStage->refreshPipelineFormat();
    }

    if (_gBufferStage) {
        _gBufferStage->refreshPipelineFormats(_currentGBufferFormats);
    }

    if (_lightStage) {
        _lightStage->setup(_gBufferStage.get(), _currentGBufferResources);
        _lightStage->setSSAOTexture(_ssaoTexture.get());
        _lightStage->refreshPipelineFormats(_currentViewportFormats);
    }

    if (_overlayStage) {
        _overlayStage->refreshPipelineFormats(_currentViewportFormats);
    }

}

void DeferredRenderPipeline::invalidateGBufferDependentViews()
{
    _cachedAlbedoSpecImageViewHandle = nullptr;
    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();

    if (_ssaoStage) {
        _ssaoStage->invalidateInputDescriptors();
    }
    if (_lightStage) {
        _lightStage->invalidateGBufferDescriptors();
    }
}

void DeferredRenderPipeline::refreshCurrentFrameResources()
{
    _currentGBufferResources  = buildDeferredGBufferResources(_gBufferRT.get());
    _currentViewportResources = buildDeferredViewportResources(_viewportRT.get());
    _currentGBufferFormats    = buildDeferredAttachmentFormats(_gBufferRT.get());
    _currentViewportFormats   = buildDeferredAttachmentFormats(_viewportRT.get());
}

void DeferredRenderPipeline::executeSSAOPass(const RenderStageContext& stageCtx)
{
    if (!_bEnableSSAO || !_ssaoStage) {
        return;
    }

    _ssaoStage->prepare(stageCtx);
    _ssaoStage->execute(stageCtx);
}

void DeferredRenderPipeline::syncFrameSettings(const RenderPipelineFrameContext& frame)
{
    (void)frame;

    if (_lightStage) {
        _lightStage->setSSAOTexture(_bEnableSSAO ? _ssaoTexture.get() : nullptr);
    }

    if (_ssaoStage) {
        _ssaoStage->setSettings(_ssaoStage->getRadius(),
                                _ssaoStage->getBias(),
                                _ssaoStage->getPower(),
                                _ssaoStage->getIntensity(),
                                _bReverseViewportY);
    }

    const auto     shadowSettings           = currentShadowSettings();
    const uint32_t shadowedPointLightBudget = shadowSettings.getEffectivePointLightCount();
    const uint32_t desiredShadowResolution  = std::max(shadowSettings.resolution, 1u);
    if (shadowSettings.isEnabled()) {
        const bool bShadowResolutionDirty = !_shadowResources.renderTarget ||
                                            _shadowResources.extent.width != desiredShadowResolution ||
                                            _shadowResources.extent.height != desiredShadowResolution;
        if (bShadowResolutionDirty) {
            requestShadowResourceRefresh();
        }
    }

    (void)shadowedPointLightBudget;
    (void)shadowSettings;
    (void)desiredShadowResolution;
    syncShadowSettings();
    updateStageFrameInputs();
}

void DeferredRenderPipeline::executeShadowPass(RenderStageContext& stageCtx)
{
    const auto shadowSettings = currentShadowSettings();
    if (_shadowStage && shadowSettings.isEnabled()) {
        _shadowStage->applySettings(shadowSettings);
        {
            YA_PERF_SCOPE(perf::sample::deferredShadow(), perf::metric::cpuTimeMs(), perf::domain::render());
            _shadowStage->prepare(stageCtx);
            _shadowStage->execute(stageCtx);
        }
        handoffShadowDepthForSampling(stageCtx.cmdBuf);
        return;
    }

    PerfState::Get().clearMetric(perf::sample::deferredShadow(), perf::metric::cpuTimeMs());
}

void DeferredRenderPipeline::handoffShadowDepthForSampling(ICommandBuffer* cmdBuf)
{
    auto* shadowDepthImage = _shadowResources.depthImage.get();
    if (!cmdBuf || !_shadowResources.renderTarget || !shadowDepthImage) {
        return;
    }

    ImageSubresourceRange shadowDepthRange{
        .aspectMask     = EImageAspect::Depth,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = _shadowResources.layerCount,
    };
    cmdBuf->transitionImageLayoutAuto(shadowDepthImage, EImageLayout::ShaderReadOnlyOptimal, &shadowDepthRange);
}

void DeferredRenderPipeline::executeGBufferPass(const RenderPipelineFrameContext& frame, const RenderStageContext& stageCtx, uint32_t vpW, uint32_t vpH)
{
    YA_PERF_SCOPE(perf::sample::deferredGBuffer(), perf::metric::cpuTimeMs(), perf::domain::render());
    _gBufferStage->prepare(stageCtx);

    auto* gbufferDepth = _currentGBufferResources.depth;
    if (!gbufferDepth) {
        return;
    }
    bool bHasAllColorAttachments = std::ranges::all_of(_currentGBufferResources.color, [](Texture* texture) {
        return texture != nullptr;
    });
    if (!bHasAllColorAttachments) {
        return;
    }

    RenderGraph graph;
    std::array<RGTextureHandle, 4> gbufferColors{};
    for (uint32_t attachmentIndex = 0; attachmentIndex < gbufferColors.size(); ++attachmentIndex) {
        gbufferColors[attachmentIndex] = graph.importTexture(
            makeDeferredImportedTextureDesc(
                *_currentGBufferResources.color[attachmentIndex],
                std::format("DeferredGBuffer.Color{}", attachmentIndex),
                EImageLayout::ShaderReadOnlyOptimal));
    }
    const auto depth = graph.importTexture(
        makeDeferredImportedTextureDesc(*gbufferDepth, "DeferredGBuffer.Depth", EImageLayout::ShaderReadOnlyOptimal));
    const Extent2D gbufferExtent = gbufferDepth->getExtent();

    [[maybe_unused]] const auto gbufferPass = graph.addPass(
        "Deferred GBuffer",
        [&](RGPassBuilder& passBuilder) {
            for (const auto handle : gbufferColors) {
                passBuilder.useColorAttachment(handle);
            }
            passBuilder.useDepthAttachment(depth);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.beginRasterRendering({
                .renderArea = Rect2D{.pos = {0, 0}, .extent = gbufferExtent.toVec2()},
                .layerCount = 1,
                .colors = {
                    {.color = gbufferColors[0], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = gbufferColors[1], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = gbufferColors[2], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = gbufferColors[3], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                },
                .depth = RGRenderContext::DepthRenderingDesc{
                    .depth       = depth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });

            float gbVpY = 0.0f;
            float gbVpH = static_cast<float>(vpH);
            if (_bReverseViewportY) {
                gbVpY = static_cast<float>(vpH);
                gbVpH = -gbVpH;
            }
            rgCtx.getCommandBuffer().setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
            rgCtx.getCommandBuffer().setScissor(0, 0, vpW, vpH);

            _gBufferStage->execute(stageCtx);
            rgCtx.endRendering();
        });

    RenderGraphExecutor executor(*_render->getResourceFactory());
    [[maybe_unused]] const bool bExecuted = executor.execute(graph, *frame.cmdBuf);
}

void DeferredRenderPipeline::executeDepthCopyPass(ICommandBuffer* cmdBuf)
{
    YA_PERF_SCOPE(perf::sample::deferredDepthCopy(), perf::metric::cpuTimeMs(), perf::domain::render());
    copyGBufferDepthToViewport(cmdBuf);
}

void DeferredRenderPipeline::executeViewportPass(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx)
{
    const uint32_t vpW = static_cast<uint32_t>(frame.viewportRect.extent.x);
    const uint32_t vpH = static_cast<uint32_t>(frame.viewportRect.extent.y);

    _lastTickCtx = {
        .view       = frame.view,
        .projection = frame.projection,
        .cameraPos  = frame.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _lastFrameInput = frame;

    auto* viewportColor = _currentViewportResources.color;
    auto* viewportDepth = _currentViewportResources.depth;
    if (!viewportColor || !viewportDepth) {
        return;
    }

    RenderGraph graph;
    const auto  color = graph.importTexture(makeDeferredImportedTextureDesc(*viewportColor, "DeferredViewport.Color", EImageLayout::ShaderReadOnlyOptimal));
    const auto  depth = graph.importTexture(makeDeferredImportedTextureDesc(*viewportDepth, "DeferredViewport.Depth", EImageLayout::ShaderReadOnlyOptimal));
    const Extent2D viewportExtent = viewportColor->getExtent();

    [[maybe_unused]] const auto viewportPass = graph.addPass(
        "Deferred Viewport",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.useColorAttachment(color);
            passBuilder.useDepthAttachment(depth);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.beginRasterRendering({
                .renderArea = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = color,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGRenderContext::DepthRenderingDesc{
                    .depth       = depth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });

            {
                YA_PERF_SCOPE(perf::sample::deferredLight(), perf::metric::cpuTimeMs(), perf::domain::render());
                _lightStage->prepare(stageCtx);
                _lightStage->execute(stageCtx);
            }

            {
                YA_PERF_SCOPE(perf::sample::deferredOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
                _overlayStage->prepare(stageCtx);
                _overlayStage->execute(stageCtx);
            }

            if (_lastFrameInput.recordViewportOverlays) {
                YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
                _lastFrameInput.recordViewportOverlays(&rgCtx.getCommandBuffer(), viewportExtent, _lastTickCtx);
            }

            rgCtx.endRendering();
        });

    RenderGraphExecutor executor(*_render->getResourceFactory());
    [[maybe_unused]] const bool bExecuted = executor.execute(graph, *frame.cmdBuf);

    auto* inputTexture = _currentViewportResources.color;
    {
        YA_PERF_SCOPE(perf::sample::renderPostProcess(), perf::metric::cpuTimeMs(), perf::domain::render());
        viewportTexture = _postProcessStage.execute(
            frame.cmdBuf, inputTexture, _lastFrameInput.viewportRect.extent, &_lastTickCtx);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Depth Copy
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::copyGBufferDepthToViewport(ICommandBuffer* cmdBuf)
{
    auto* gbufferDepth  = _currentGBufferResources.depth;
    auto* viewportDepth = _currentViewportResources.depth;
    if (!cmdBuf || !gbufferDepth || !viewportDepth) {
        return;
    }

    auto* srcImage = gbufferDepth->getImage();
    auto* dstImage = viewportDepth->getImage();
    if (!srcImage || !dstImage) {
        return;
    }

    RenderGraph graph;
    const auto  src = graph.importTexture(makeDeferredImportedTextureDesc(*gbufferDepth, "DeferredDepthCopy.Src", EImageLayout::ShaderReadOnlyOptimal));
    const auto  dst = graph.importTexture(makeDeferredImportedTextureDesc(*viewportDepth, "DeferredDepthCopy.Dst", EImageLayout::ShaderReadOnlyOptimal));

    [[maybe_unused]] const auto pass = graph.addPass(
        "Deferred Depth Copy",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.transferSrc(src);
            passBuilder.transferDst(dst);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.copyTexture(
                src,
                dst,
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
                    .extentWidth  = gbufferDepth->getWidth(),
                    .extentHeight = gbufferDepth->getHeight(),
                    .extentDepth  = 1,
                });
        });

    RenderGraphExecutor executor(*_render->getResourceFactory());
    [[maybe_unused]] const bool bExecuted = executor.execute(graph, *cmdBuf);
}

// ═══════════════════════════════════════════════════════════════════════
// Viewport Pass
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };

    requestViewportResize(newExtent);
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::renderSettingsGUI()
{
    renderGeneralSettingsGUI();

    if (ImGui::TreeNode("Lighting")) {
        renderLightingSettingsGUI();
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Ambient Occlusion")) {
        renderAOSettingsGUI();
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Post Process")) {
        renderPostProcessSettingsGUI();
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Shadows")) {
        renderShadowSettingsGUI();
        ImGui::TreePop();
    }
}

void DeferredRenderPipeline::renderGeneralSettingsGUI()
{
    ImGui::Checkbox("GBuffer Reverse Viewport Y", &_bReverseViewportY);
    ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");
}

void DeferredRenderPipeline::renderLightingSettingsGUI()
{
    if (_lightStage) {
        _lightStage->renderSettingsGUI();
    }
}

void DeferredRenderPipeline::renderAOSettingsGUI()
{
    if (ImGui::Checkbox("Enable SSAO", &_bEnableSSAO)) {
        ConfigManager::Editor(DEFERRED_PIPELINE_CONFIG_DOC_NAME)
            .set(DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SSAO, _bEnableSSAO);
    }

    if (_bEnableSSAO && _ssaoStage) {
        _ssaoStage->renderSettingsGUI();
    }
}

void DeferredRenderPipeline::renderPostProcessSettingsGUI()
{
    _postProcessStage.renderSettingsGUI();
}

void DeferredRenderPipeline::renderShadowSettingsGUI()
{
    if (!_shadowSettings) {
        return;
    }
    const ShadowSettings currentShadowSettings = *_shadowSettings;
    ShadowSettings       pendingShadowSettings = currentShadowSettings;
    bool                 bShadowSettingsDirty  = false;

    bool bShadowEnabled = pendingShadowSettings.isEnabled();
    if (ImGui::Checkbox("Enable Shadow Mapping", &bShadowEnabled)) {
        if (bShadowEnabled) {
            if (pendingShadowSettings.quality == EShadowQuality::Off) {
                pendingShadowSettings.applyQualityPreset(EShadowQuality::Medium);
            }
        }
        else {
            pendingShadowSettings.quality = EShadowQuality::Off;
        }
        bShadowSettingsDirty = true;
    }

    if (pendingShadowSettings.isEnabled()) {
        static const char* qualityNames[] = {"Low", "Medium", "High", "Ultra"};
        int                qualityIdx     = std::max(0, static_cast<int>(pendingShadowSettings.quality) - 1);
        if (ImGui::Combo("Quality Preset", &qualityIdx, qualityNames, IM_ARRAYSIZE(qualityNames))) {
            auto newQuality = static_cast<EShadowQuality::T>(qualityIdx + 1);
            pendingShadowSettings.applyQualityPreset(newQuality);
            bShadowSettingsDirty = true;
        }

        if (ImGui::Checkbox("Directional Shadow", &pendingShadowSettings.directionalEnabled)) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Point Light Shadow", &pendingShadowSettings.pointLightEnabled)) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Point Light Indirect Draw", &pendingShadowSettings.pointLightUseIndirect)) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Point Light Indirect Cull", &pendingShadowSettings.pointLightIndirectCullEnabled)) {
            bShadowSettingsDirty = true;
        }
        int maxPL = static_cast<int>(pendingShadowSettings.maxPointLightShadows);
        if (ImGui::SliderInt("Max Point Shadows", &maxPL, 0, MAX_POINT_LIGHTS)) {
            pendingShadowSettings.maxPointLightShadows = static_cast<uint32_t>(maxPL);
            bShadowSettingsDirty = true;
        }

        int shadowResolution = static_cast<int>(pendingShadowSettings.resolution);
        if (ImGui::DragInt("Shadow Resolution", &shadowResolution, 16.0f, 128, 8192, "%d")) {
            pendingShadowSettings.resolution = static_cast<uint32_t>(std::clamp(shadowResolution, 128, 8192));
            bShadowSettingsDirty = true;
        }

        if (ImGui::DragFloat("Depth Bias", &pendingShadowSettings.bias, 0.0001f, 0.0f, 0.1f, "%.5f")) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::DragFloat("Normal Bias", &pendingShadowSettings.normalBias, 0.0001f, 0.0f, 0.1f, "%.5f")) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::DragFloat("Directional Distance", &pendingShadowSettings.directionalDistance, 0.5f, 1.0f, 500.0f, "%.1f")) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Stable Directional Fit", &pendingShadowSettings.directionalStableFit)) {
            bShadowSettingsDirty = true;
        }
        int directionalCascades = static_cast<int>(pendingShadowSettings.directionalCascades);
        if (ImGui::SliderInt("Directional Cascades", &directionalCascades, 0, 4)) {
            pendingShadowSettings.directionalCascades = static_cast<uint32_t>(directionalCascades);
            bShadowSettingsDirty = true;
        }

        static const char* filterNames[] = {"Hard", "PCF Low", "PCF High"};
        int                currentFilter = static_cast<int>(pendingShadowSettings.filter);
        if (ImGui::Combo("Shadow Filter", &currentFilter, filterNames, IM_ARRAYSIZE(filterNames))) {
            pendingShadowSettings.filter = static_cast<EShadowFilter::T>(currentFilter);
            bShadowSettingsDirty = true;
        }
    }

    if (bShadowSettingsDirty) {
        queueShadowSettingsChange(pendingShadowSettings);
    }
}

void DeferredRenderPipeline::renderTechnicalGUI()
{
    renderPerformanceGUI();
    renderStageInternalsGUI();
}

void DeferredRenderPipeline::renderPerformanceGUI()
{
    auto& perf = profiling::metrics();

    auto metric = [&perf](FName sampleKey, FName metricKey) {
        return perf.getDisplayValue(sampleKey, metricKey);
    };
    auto cpu = [&metric](FName sampleKey) {
        return metric(sampleKey, perf::metric::cpuTimeMs());
    };

    const float frameCpuMs        = cpu(perf::sample::renderFrame());
    const float frameGpuMs        = metric(perf::sample::renderFrame(), perf::metric::gpuTimeMs());
    const float logicMs           = cpu(perf::sample::frameLogic());
    const float renderMs          = cpu(perf::sample::frameRender());
    const float automationMs      = cpu(perf::sample::frameAutomation());
    const float unaccountedMs     = cpu(perf::sample::frameUnaccounted());
    const float extractMs         = cpu(perf::sample::renderExtract());
    const float runtimeMs         = cpu(perf::sample::renderRuntime());
    const float prepareFrameMs    = cpu(perf::sample::renderPrepareFrame());
    const float waitIdleMs        = cpu(perf::sample::renderWaitIdle());
    const float beginMs           = cpu(perf::sample::renderBegin());
    const float waitFenceMs       = cpu(perf::sample::vulkanWaitFence());
    const float acquireMs         = cpu(perf::sample::vulkanAcquire());
    const float worldMs           = cpu(perf::sample::renderWorld());
    const float deferredTickMs    = cpu(perf::sample::deferredTick());
    const float shadowMs          = cpu(perf::sample::deferredShadow());
    const float gbufferMs         = cpu(perf::sample::deferredGBuffer());
    const float depthCopyMs       = cpu(perf::sample::deferredDepthCopy());
    const float lightMs           = cpu(perf::sample::deferredLight());
    const float overlayMs         = cpu(perf::sample::deferredOverlay());
    const float viewportOverlayMs = cpu(perf::sample::renderViewportOverlay());
    const float postProcessMs     = cpu(perf::sample::renderPostProcess());
    const float presentationMs    = cpu(perf::sample::renderPresentation());
    const float renderCallbacksMs  = cpu(perf::sample::frameRenderCallbacks());
    const float submitMs          = cpu(perf::sample::renderSubmit());
    const float presentMs         = cpu(perf::sample::vulkanPresent());

    ImGui::Text("CPU frame: %.3f ms", frameCpuMs);
    ImGui::Text("GPU frame: %.3f ms", frameGpuMs);

    if (ImGui::TreeNode("Frame Cycle", "Frame Cycle  %.3f ms", frameCpuMs)) {
        drawPerfLeaf("Logic", logicMs, frameCpuMs);
        drawPerfNode("Render", renderMs, [&]() {
            drawPerfLeaf("Extract", extractMs, renderMs);
            drawPerfNode("Runtime", runtimeMs, [&]() {
                drawPerfNode("PrepareFrame", prepareFrameMs, [&]() {
                    drawPerfLeaf("WaitIdle", waitIdleMs, prepareFrameMs);
                    drawPerfNode("Begin", beginMs, [&]() {
                        drawPerfLeaf("WaitFence", waitFenceMs, beginMs);
                        drawPerfLeaf("Acquire", acquireMs, beginMs);
                    });
                });
                drawPerfNode("World", worldMs, [&]() {
                    drawPerfNode("Deferred", deferredTickMs, [&]() {
                        drawPerfLeaf("Shadow", shadowMs, deferredTickMs);
                        drawPerfLeaf("GBuffer", gbufferMs, deferredTickMs);
                        drawPerfLeaf("DepthCopy", depthCopyMs, deferredTickMs);
                        drawPerfLeaf("Light", lightMs, deferredTickMs);
                        drawPerfLeaf("Overlay", overlayMs, deferredTickMs);
                    });
                    drawPerfLeaf("ViewportOverlay", viewportOverlayMs, worldMs);
                    drawPerfLeaf("PostProcess", postProcessMs, worldMs);
                });
                drawPerfLeaf("Presentation", presentationMs, runtimeMs);
                drawPerfLeaf("RenderCallbacks", renderCallbacksMs, runtimeMs);
                drawPerfNode("Submit", submitMs, [&]() {
                    drawPerfLeaf("Present", presentMs, submitMs);
                });
            });
        });
        drawPerfLeaf("Automation", automationMs, frameCpuMs);
        drawPerfLeaf("Unaccounted", unaccountedMs, frameCpuMs);
        ImGui::TreePop();
    }

    ImGui::Text("Draw items: %u", _lastDrawCount);
    ImGui::Text("Point lights: %u", _lastPointLightCount);
}

void DeferredRenderPipeline::renderStageInternalsGUI()
{
    if (_shadowStage) _shadowStage->renderGUI();
    if (_gBufferStage) _gBufferStage->renderGUI();
    if (_ssaoStage && ImGui::TreeNode("SSAO")) {
        _ssaoStage->renderTechnicalGUI();
        ImGui::TreePop();
    }
    if (_lightStage && ImGui::TreeNode("Lighting")) {
        _lightStage->renderTechnicalGUI();
        ImGui::TreePop();
    }
    if (_overlayStage) _overlayStage->renderGUI();
    if (ImGui::TreeNode("Post Process")) {
        _postProcessStage.renderTechnicalGUI();
        ImGui::TreePop();
    }
}

void DeferredRenderPipeline::renderGUI(bool bRenderTreeNode)
{
    if (bRenderTreeNode && !ImGui::TreeNode("Deferred Pipeline")) return;

    renderSettingsGUI();

    if (ImGui::TreeNode("Pipeline Internals")) {
        renderTechnicalGUI();
        ImGui::TreePop();
    }

    if (bRenderTreeNode) { ImGui::TreePop(); }
}

} // namespace ya
