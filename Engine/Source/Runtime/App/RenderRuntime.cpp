#include "RenderRuntime.h"

#include "App.h"
#include "DebugRenderSystem.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/2D/Render2D.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

#include <limits>

namespace ya
{

void RenderRuntime::onViewportResized(Rect2D rect)
{
    _viewportRect = rect;

    if (auto* pipeline = getActivePipeline()) {
        pipeline->onViewportResized(rect);
    }
}

void RenderRuntime::renderFrame(const FrameInput& input)
{
    YA_PROFILE_SCOPE("RenderRuntime::renderFrame");
    YA_PERF_SCOPE(perf::sample::renderRuntime(), perf::metric::cpuTimeMs(), perf::domain::render());

    applyPendingRenderPipelineSwitch();

    int32_t                         imageIndex = -1;
    std::shared_ptr<ICommandBuffer> cmdBuf;
    if (!prepareFrame(input, imageIndex, cmdBuf)) {
        return;
    }

    {
        YA_PERF_SCOPE(perf::sample::renderWorld(), perf::metric::cpuTimeMs(), perf::domain::render());
        renderWorldFrame(input, cmdBuf.get());
    }
    syncEditorFrame(input.editor.target);
    renderPresentationPass(input.pipeline.deltaTime,
                           input.automation.recordPresentationCapture,
                           cmdBuf.get());
    {
        YA_PERF_SCOPE(perf::sample::renderSubmit(), perf::metric::cpuTimeMs(), perf::domain::render());
        submitFrame(imageIndex, cmdBuf.get());
    }
}

bool RenderRuntime::prepareFrame(const FrameInput& input, int32_t& imageIndex, std::shared_ptr<ICommandBuffer>& cmdBuf)
{
    YA_PROFILE_FUNCTION()
    ensureViewportRectInitialized(input);
    _viewportFrameBufferScale = input.pipeline.viewportFrameBufferScale;
    return beginFrameCommandBuffer(imageIndex, cmdBuf);
}

void RenderRuntime::renderWorldFrame(const FrameInput& input, ICommandBuffer* cmdBuf)
{
    beginViewportPassAndTickPipeline(input, cmdBuf);
}

void RenderRuntime::syncEditorFrame(EditorLayer* editorLayer)
{
    updateEditorViewportContext(editorLayer);
}

IRenderPipeline* RenderRuntime::getActivePipeline() const
{
    if (_renderPipeline == ERenderPipeline::Forward && _forwardPipeline) {
        return _forwardPipeline.get();
    }
    if (_renderPipeline == ERenderPipeline::Deferred && _deferredPipeline) {
        return _deferredPipeline.get();
    }
    if (_forwardPipeline) {
        return _forwardPipeline.get();
    }
    if (_deferredPipeline) {
        return _deferredPipeline.get();
    }
    return nullptr;
}

IRenderPipelineExecution* RenderRuntime::getActivePipelineExecution() const
{
    return getActivePipeline();
}

IRenderPipelineSettingsUI* RenderRuntime::getActivePipelineSettingsUI() const
{
    return getActivePipeline();
}

IRenderPipelineDebugUI* RenderRuntime::getActivePipelineDebugUI() const
{
    return getActivePipeline();
}

IRenderPipelineDebugOutputs* RenderRuntime::getActivePipelineDebugOutputs() const
{
    return getActivePipeline();
}

bool RenderRuntime::isShadowMappingEnabled() const
{
    if (auto* pipeline = getActivePipelineDebugOutputs()) {
        return pipeline->isShadowMappingEnabled();
    }
    return false;
}

bool RenderRuntime::isMirrorRenderingEnabled() const
{
    return false;
}

bool RenderRuntime::hasMirrorRenderResult() const
{
    return false;
}

IImageView* RenderRuntime::getShadowDirectionalDepthIV() const
{
    if (auto* pipeline = getActivePipelineDebugOutputs()) {
        return pipeline->getShadowDirectionalDepthIV();
    }
    return nullptr;
}

IImageView* RenderRuntime::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (auto* pipeline = getActivePipelineDebugOutputs()) {
        return pipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    return nullptr;
}

std::shared_ptr<RenderImage> RenderRuntime::getPostprocessOutputImageShared() const
{
    if (_renderPipeline == ERenderPipeline::Forward && _forwardPipeline) {
        return _forwardPipeline->getPostprocessOutputImageShared();
    }
    if (_renderPipeline == ERenderPipeline::Deferred && _deferredPipeline) {
        return _deferredPipeline->getPostprocessOutputImageShared();
    }
    return nullptr;
}

std::shared_ptr<RenderImage> RenderRuntime::getPresentationImageShared() const
{
    return getCurrentPresentationImageShared();
}

std::shared_ptr<RenderImage> RenderRuntime::getCurrentPresentationImageShared() const
{
    const auto imageIndex = getCurrentPresentationImageIndex();
    if (imageIndex >= _presentationImages.size()) {
        return nullptr;
    }

    return _presentationImages[imageIndex];
}

uint32_t RenderRuntime::getCurrentPresentationImageIndex() const
{
    if (!_render) {
        return std::numeric_limits<uint32_t>::max();
    }

    auto* swapchain = _render->getSwapchain();
    if (!swapchain) {
        return std::numeric_limits<uint32_t>::max();
    }

    return swapchain->getCurImageIndex();
}

bool RenderRuntime::isPostprocessingEnabled() const
{
    if (auto* pipeline = getActivePipelineDebugOutputs()) {
        return pipeline->isPostprocessingEnabled();
    }
    return false;
}

RenderPipelineDebugOutputCatalog RenderRuntime::buildPipelineDebugOutputCatalog() const
{
    RenderPipelineDebugOutputCatalog catalog{};
    auto* pipeline = getActivePipelineDebugOutputs();
    if (!pipeline) {
        return catalog;
    }

    catalog.bShadowMappingEnabled  = pipeline->isShadowMappingEnabled();
    catalog.shadowDepthImage       = pipeline->getShadowDepthImage();
    catalog.viewportDepthTexture   = pipeline->getViewportDepthTexture();
    catalog.shadowDirectionalDepth = pipeline->getShadowDirectionalDepthIV();
    catalog.bPostprocessingEnabled = pipeline->isPostprocessingEnabled();

    if (_renderPipeline == ERenderPipeline::Forward && _forwardPipeline) {
        catalog.viewportOutputImageOwner    = _forwardPipeline->getViewportOutputImageShared();
        catalog.postprocessOutputImageOwner = _forwardPipeline->getPostprocessOutputImageShared();
        catalog.bloomExtractOwner           = _forwardPipeline->getBloomExtractImageShared();
        catalog.bloomBlurOwner              = _forwardPipeline->getBloomBlurImageShared();
        catalog.bloomCompositeOwner         = _forwardPipeline->getBloomCompositeImageShared();
        return catalog;
    }

    if (_renderPipeline == ERenderPipeline::Deferred && _deferredPipeline) {
        catalog.viewportOutputImageOwner    = _deferredPipeline->getViewportOutputImageShared();
        catalog.postprocessOutputImageOwner = _deferredPipeline->getPostprocessOutputImageShared();
        catalog.bloomExtractOwner           = _deferredPipeline->getBloomExtractImageShared();
        catalog.bloomBlurOwner              = _deferredPipeline->getBloomBlurImageShared();
        catalog.bloomCompositeOwner         = _deferredPipeline->getBloomCompositeImageShared();
    }

    return catalog;
}

Extent2D RenderRuntime::getViewportExtent() const
{
    if (auto* pipeline = getActivePipelineExecution()) {
        return pipeline->getViewportExtent();
    }
    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        return Extent2D::fromVec2(_viewportRect.extent);
    }
    return {};
}

DeferredPipelineDebugViews RenderRuntime::getDeferredPipelineDebugViews() const
{
    if (_renderPipeline == ERenderPipeline::Deferred && _deferredPipeline) {
        return _deferredPipeline->buildDebugViews();
    }
    return {};
}

RenderTargetEditorCatalog RenderRuntime::buildRenderTargetEditorCatalog() const
{
    RenderTargetEditorCatalog catalog{};

    if (auto presentationImage = getCurrentPresentationImageShared()) {
        catalog.entries.push_back({
            .label            = "Presentation",
            .owner            = RenderTargetEditorCatalog::Entry::EOwner::Presentation,
            .colorFormats     = {_render->getSwapchain()->getFormat()},
            .colorAttachments = {presentationImage},
            .extent           = presentationImage->getExtent(),
            .frameBufferCount = _render->getSwapchain()->getImageCount(),
            .bSwapChainTarget = true,
            .bEditable        = false,
        });
    }
    if (auto* pipeline = getActivePipelineDebugUI()) {
        pipeline->appendRenderTargetEditorEntries(catalog);
    }

    return catalog;
}

DebugRenderSystem& RenderRuntime::getDebugRenderSystem() const
{
    return DebugRenderSystem::get();
}

void RenderRuntime::initActivePipeline()
{
    int winW = 0;
    int winH = 0;
    _render->getWindowSize(winW, winH);

    if (_renderPipeline == ERenderPipeline::Forward) {
        _forwardPipeline = ya::makeShared<ForwardRenderPipeline>();
        _forwardPipeline->init(ForwardRenderPipeline::InitDesc{
            .render                       = _render,
            .windowW                      = winW,
            .windowH                      = winH,
            .shadowSettings               = _app ? &_app->getShadowSettings() : nullptr,
            .getFrameIndex                = _app
                ? [this]() -> uint64_t { return _app ? _app->getFrameIndex() : 0; }
                : std::function<uint64_t()>{},
            .getElapsedTimeSeconds        = _app
                ? [this]() -> double { return _app ? static_cast<double>(_app->getElapsedTimeMS()) / 1000.0 : 0.0; }
                : std::function<double()>{},
            .getActiveScene               = _app
                ? [this]() -> Scene*
                  {
                      if (!_app || !_app->getSceneManager()) {
                          return nullptr;
                      }
                      return _app->getSceneManager()->getActiveScene();
                  }
                : std::function<Scene*()>{},
            .getResourceResolveSystem     = _app
                ? [this]() -> ResourceResolveSystem*
                  {
                      return _app ? _app->getResourceResolveSystem() : nullptr;
                  }
                : std::function<ResourceResolveSystem*()>{},
            .getSceneSkyboxDescriptorSet  = [this](Scene* scene)
            {
                return getSceneSkyboxDescriptorSet(scene);
            },
            .getSceneEnvironmentLightingDescriptorSet = [this](Scene* scene)
            {
                return getSceneEnvironmentLightingDescriptorSet(scene);
            },
        });
    }
    else {
        _deferredPipeline = ya::makeShared<DeferredRenderPipeline>();
        _deferredPipeline->init(DeferredRenderPipeline::InitDesc{
            .render                   = _render,
            .windowW                  = winW,
            .windowH                  = winH,
            .shadowSettings           = _app ? &_app->getShadowSettings() : nullptr,
            .automationShadowOverrides = _app ? &_app->getDesc().automation.shadow : nullptr,
            .queueFrameTask           = _app
                ? [this](std::function<void()> task)
                  {
                      if (_app) {
                          _app->taskManager.registerFrameTask(std::move(task));
                      }
                  }
                : std::function<void(std::function<void()>)>{},
            .environmentLightingDSL = _sharedResourceProvider.getEnvironmentLightingDescriptorSetLayout(),
            .getSceneEnvironmentLightingDescriptorSet = [this](Scene* scene)
            {
                return getSceneEnvironmentLightingDescriptorSet(scene);
            },
            .resolveSceneEnvironmentLightingTextures = [this](Scene* scene)
            {
                return resolveSceneEnvironmentLightingTextures(scene);
            },
            .getSceneSkyboxDescriptorSet = [this](Scene* scene)
            {
                return getSceneSkyboxDescriptorSet(scene);
            },
            .getDebugRenderSystem = [this]() -> DebugRenderSystem&
            {
                return getDebugRenderSystem();
            },
            .getActiveScene = [this]() -> Scene*
            {
                if (!_app || !_app->getSceneManager()) {
                    return nullptr;
                }
                return _app->getSceneManager()->getActiveScene();
            },
            .getResourceResolveSystem = [this]() -> ResourceResolveSystem*
            {
                return _app ? _app->getResourceResolveSystem() : nullptr;
            },
        });
    }

    if (auto* pipeline = getActivePipelineExecution()) {
        Render2D::init(_render, pipeline->getViewportColorFormat(), pipeline->getViewportDepthFormat());
    }
}

void RenderRuntime::shutdownActivePipeline()
{
    Render2D::destroy();

    if (_forwardPipeline) {
        _forwardPipeline->shutdown();
        _forwardPipeline.reset();
    }
    if (_deferredPipeline) {
        _deferredPipeline->shutdown();
        _deferredPipeline.reset();
    }
}

void RenderRuntime::applyPendingRenderPipelineSwitch()
{
    if (_pendingRenderPipeline == _renderPipeline) {
        return;
    }
    YA_PROFILE_FUNCTION_LOG();

    YA_CORE_INFO("Switching render pipeline: {} -> {}",
                 _renderPipeline == ERenderPipeline::Forward ? "Forward" : "Deferred",
                 _pendingRenderPipeline == ERenderPipeline::Forward ? "Forward" : "Deferred");

    _render->waitIdle();

    shutdownActivePipeline();
    _renderPipeline = _pendingRenderPipeline;
    initActivePipeline();

    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        onViewportResized(_viewportRect);
    }
}

} // namespace ya
