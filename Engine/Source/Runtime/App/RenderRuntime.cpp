#include "RenderRuntime.h"

#include "App.h"
#include "DebugRenderSystem.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/2D/Render2D.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

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
    renderViewportPassOverlays(input.pipeline, input.overlay, cmdBuf);
    endViewportPass(cmdBuf);
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

IRenderTarget* RenderRuntime::getShadowDepthRT() const
{
    if (auto* pipeline = getActivePipelineDebugOutputs()) {
        return pipeline->getShadowDepthRT();
    }
    return nullptr;
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

Texture* RenderRuntime::getPostprocessOutputTexture() const
{
    if (auto* pipeline = getActivePipelineDebugOutputs()) {
        return pipeline->getPostprocessOutputTexture();
    }
    return nullptr;
}

Texture* RenderRuntime::getBloomExtractTexture() const
{
    if (auto* pipeline = getActivePipelineDebugOutputs()) {
        return pipeline->getBloomExtractTexture();
    }
    return nullptr;
}

Texture* RenderRuntime::getBloomBlurTexture() const
{
    if (auto* pipeline = getActivePipelineDebugOutputs()) {
        return pipeline->getBloomBlurTexture();
    }
    return nullptr;
}

Texture* RenderRuntime::getBloomCompositeTexture() const
{
    if (auto* pipeline = getActivePipelineDebugOutputs()) {
        return pipeline->getBloomCompositeTexture();
    }
    return nullptr;
}

Texture* RenderRuntime::getPresentationTexture() const
{
    if (!_screenRT) {
        return nullptr;
    }

    auto* frameBuffer = const_cast<IRenderTarget*>(_screenRT.get())->getCurFrameBuffer();
    return frameBuffer ? frameBuffer->getColorTexture(0) : nullptr;
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
    auto*                            pipeline = getActivePipelineDebugOutputs();
    if (!pipeline) {
        return catalog;
    }

    catalog.bShadowMappingEnabled  = pipeline->isShadowMappingEnabled();
    catalog.shadowDepthRT          = pipeline->getShadowDepthRT();
    catalog.shadowDirectionalDepth = pipeline->getShadowDirectionalDepthIV();
    catalog.postprocessOutput      = pipeline->getPostprocessOutputTexture();
    catalog.bloomExtract           = pipeline->getBloomExtractTexture();
    catalog.bloomBlur              = pipeline->getBloomBlurTexture();
    catalog.bloomComposite         = pipeline->getBloomCompositeTexture();
    catalog.bPostprocessingEnabled = pipeline->isPostprocessingEnabled();
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

IRenderTarget* RenderRuntime::getActiveViewportRT() const
{
    if (auto* pipeline = getActivePipelineExecution()) {
        return pipeline->getViewportRT();
    }
    return nullptr;
}

DeferredPipelineDebugViews RenderRuntime::getDeferredPipelineDebugViews() const
{
    DeferredPipelineDebugViews views{};
    if (_renderPipeline == ERenderPipeline::Deferred && _deferredPipeline) {
        views.gBufferRT   = _deferredPipeline->getGBufferRT();
        views.ssaoTexture = _deferredPipeline->getSSAOTexture();
    }
    return views;
}

RenderTargetEditorCatalog RenderRuntime::buildRenderTargetEditorCatalog() const
{
    RenderTargetEditorCatalog catalog{};

    if (_screenRT) {
        catalog.entries.push_back({
            .label     = "Presentation",
            .rt        = _screenRT.get(),
            .owner     = RenderTargetEditorCatalog::Entry::EOwner::Presentation,
            .bEditable = false,
        });
    }
    if (_forwardPipeline) {
        catalog.entries.push_back({
            .label = "Forward Viewport",
            .rt    = _forwardPipeline->getViewportRT(),
            .owner = RenderTargetEditorCatalog::Entry::EOwner::ForwardViewport,
        });
        catalog.entries.push_back({
            .label = "Forward Shadow",
            .rt    = _forwardPipeline->getShadowDepthRT(),
            .owner = RenderTargetEditorCatalog::Entry::EOwner::ForwardShadow,
        });
    }
    if (_deferredPipeline) {
        catalog.entries.push_back({
            .label = "Deferred GBuffer",
            .rt    = _deferredPipeline->getGBufferRT(),
            .owner = RenderTargetEditorCatalog::Entry::EOwner::DeferredGBuffer,
        });
        catalog.entries.push_back({
            .label = "Deferred Viewport",
            .rt    = _deferredPipeline->getViewportRT(),
            .owner = RenderTargetEditorCatalog::Entry::EOwner::DeferredViewport,
        });
        catalog.entries.push_back({
            .label = "Deferred Shadow",
            .rt    = _deferredPipeline->getShadowDepthRT(),
            .owner = RenderTargetEditorCatalog::Entry::EOwner::DeferredShadow,
        });
    }

    return catalog;
}

void RenderRuntime::setDeferredSharedDepthFormat(EFormat::T format)
{
    if (!_deferredPipeline) {
        return;
    }

    if (auto* gbufferRT = _deferredPipeline->getGBufferRT()) {
        gbufferRT->setDepthAttachmentFormat(format);
    }
    if (auto* viewportRT = _deferredPipeline->getViewportRT()) {
        viewportRT->setDepthAttachmentFormat(format);
    }
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
            .render         = _render,
            .windowW        = winW,
            .windowH        = winH,
            .shadowSettings = _app ? &_app->getShadowSettings() : nullptr,
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

    if (_renderPipeline == ERenderPipeline::Forward) {
        Render2D::init(_render, ForwardRenderPipeline::VIEWPORT_COLOR_FORMAT, ForwardRenderPipeline::DEPTH_FORMAT);
    }
    else {
        Render2D::init(_render, _deferredPipeline->VIEWPORT_COLOR_FORMAT, _deferredPipeline->DEPTH_FORMAT);
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

    if (auto* pipeline = getActivePipelineExecution(); pipeline && pipeline->hasOpenViewportPass()) {
        YA_CORE_WARN("Skipping render pipeline switch while a viewport pass is still open");
        return;
    }

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
