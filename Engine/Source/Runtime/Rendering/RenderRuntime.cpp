#include "RenderRuntime.h"

#include "Runtime/Application/App.h"
#include "Runtime/Rendering/Services/DebugRenderSystem.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Runtime/Rendering/Deferred/DeferredRenderPipeline.h"
#include "Runtime/Rendering/Common/RenderViewportOverlayRecorder.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/2D/Render2D.h"
#include "Runtime/Rendering/Forward/ForwardRenderPipeline.h"

#include <limits>

namespace ya
{

namespace
{

const char* toString(RenderRuntime::ERenderPipeline pipeline)
{
    return pipeline == RenderRuntime::ERenderPipeline::Forward ? "Forward" : "Deferred";
}

} // namespace

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

    // Frame lifecycle (FG-603):
    //   1. prepareFrame: acquire swapchain image + begin command buffer (graph-external).
    //   2. renderWorldFrame: Deferred/Forward world graph via the pipeline-owned executor.
    //   3. renderPresentationPass: per-swapchain-image presentation graph; screenshot
    //      readback is appended inside that graph (FG-601), never recorded outside.
    //   4. submitFrame: submit + present (graph-external).
    //
    // Presentation intentionally keeps its own executor (FG-602 decision): the
    // swapchain-image scope and acquire/present lifecycle stay outside the world
    // graph, so merging the two executors would only spread swapchain semantics
    // into the render pipelines without removing real duplicated state.

    applyPendingRenderPipelineSwitch();
    applyPendingRenderTargetFormatCommands();

    // All Render2D pipeline changes must happen before command recording. The
    // post-process/viewport target format can differ from the initial viewport
    // format (for example HDR R16G16B16A16_SFLOAT), so resolve it from the
    // active pipeline before beginning this frame's command buffer.
    if (input.uiSceneRoot) {
        auto uiTarget = getViewportDisplayImageShared();
        if (uiTarget) {
            prepareRender2DComposePassPipeline(
                FRender2DComposePassDesc{
                    .kind = ERender2DComposePassKind::RuntimeUIComposite,
                },
                uiTarget->getFormat());
        }
    }

    int32_t                         imageIndex = -1;
    std::shared_ptr<ICommandBuffer> cmdBuf;
    if (!prepareFrame(input, imageIndex, cmdBuf)) {
        return;
    }

    {
        YA_PERF_SCOPE(perf::sample::renderWorld(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (_bWorldSceneRenderEnabled) {
            renderWorldFrame(input, cmdBuf.get());
        }
    }
    // Game UI composites AFTER the world graph and its post-processing, so UI
    // never enters bloom or tonemapping (graph-external, manual transitions).
    if (input.uiSceneRoot) {
        auto uiTarget = getViewportDisplayImageShared();
        if (uiTarget) {
            recordRender2DComposePass(cmdBuf.get(),
                                      *uiTarget,
                                      nullptr,
                                      input.uiSceneRoot,
                                      FRender2DComposePassDesc{
                                          .kind = ERender2DComposePassKind::RuntimeUIComposite,
                                          .logicalViewportExtent = Extent2D{
                                              .width  = static_cast<uint32_t>(input.pipeline.viewportRect.extent.x),
                                              .height = static_cast<uint32_t>(input.pipeline.viewportRect.extent.y),
                                          },
            });
        }
    }
    // Module viewport composition (editor overlays) records after the world
    // graph and the runtime game UI pass, still before the presentation graph.
    // This keeps every manual compose segment in one place inside renderFrame;
    // modules only provide content through the registered callback.
    if (input.viewportCompose.recordCompose) {
        input.viewportCompose.recordCompose(cmdBuf.get());
    }
    renderPresentationPass(input.pipeline.deltaTime, input.presentationExtensions, cmdBuf.get());
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
    YA_PROFILE_FUNCTION();

    beginViewportPassAndTickPipeline(input, cmdBuf);
}

IRenderPipeline* RenderRuntime::getActivePipeline() const
{
    if (auto* pipeline = getSelectedForwardPipeline()) {
        return pipeline;
    }
    if (auto* pipeline = getSelectedDeferredPipeline()) {
        return pipeline;
    }
    if (_forwardPipeline) {
        return _forwardPipeline.get();
    }
    if (_deferredPipeline) {
        return _deferredPipeline.get();
    }
    return nullptr;
}

uint64_t RenderRuntime::getFrameIndex() const
{
    return _app ? _app->getFrameIndex() : 0;
}

double RenderRuntime::getElapsedTimeSeconds() const
{
    return _app ? static_cast<double>(_app->getElapsedTimeMS()) / 1000.0 : 0.0;
}

Scene* RenderRuntime::getActiveScene() const
{
    if (!_app || !_app->getSceneServices().getSceneManager()) {
        return nullptr;
    }
    return _app->getSceneServices().getActiveScene();
}

ResourceResolveSystem* RenderRuntime::getResourceResolveSystem() const
{
    return _app ? _app->getResourceResolveSystem() : nullptr;
}

bool RenderRuntime::isShadowMappingEnabled() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->isShadowMappingEnabled();
    }
    return false;
}

IImageView* RenderRuntime::getShadowDirectionalDepthIV() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getShadowDirectionalDepthIV();
    }
    return nullptr;
}

IImageView* RenderRuntime::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    return nullptr;
}

std::shared_ptr<RenderImage> RenderRuntime::getPostprocessOutputImageShared() const
{
    if (auto* pipeline = getSelectedForwardPipeline()) {
        return pipeline->getPostprocessOutputImageShared();
    }
    if (auto* pipeline = getSelectedDeferredPipeline()) {
        return pipeline->getPostprocessOutputImageShared();
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
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->isPostprocessingEnabled();
    }
    return false;
}

RenderPipelineDebugOutputCatalog RenderRuntime::buildPipelineDebugOutputCatalog() const
{
    RenderPipelineDebugOutputCatalog catalog{};
    auto* pipeline = getActivePipeline();
    if (!pipeline) {
        return catalog;
    }

    catalog.bShadowMappingEnabled  = pipeline->isShadowMappingEnabled();
    catalog.shadowDepthImage       = pipeline->getShadowDepthImage();
    catalog.viewportDepthImageOwner = pipeline->getViewportDepthImageShared();
    catalog.shadowDirectionalDepth = pipeline->getShadowDirectionalDepthIV();
    catalog.bPostprocessingEnabled = pipeline->isPostprocessingEnabled();

    if (auto* selectedForward = getSelectedForwardPipeline()) {
        catalog.viewportOutputImageOwner    = selectedForward->getViewportOutputImageShared();
        catalog.postprocessOutputImageOwner = selectedForward->getPostprocessOutputImageShared();
        catalog.bloomExtractOwner           = selectedForward->getBloomExtractImageShared();
        catalog.bloomBlurOwner              = selectedForward->getBloomBlurImageShared();
        catalog.bloomCompositeOwner         = selectedForward->getBloomCompositeImageShared();
        return catalog;
    }

    if (auto* selectedDeferred = getSelectedDeferredPipeline()) {
        catalog.viewportOutputImageOwner    = selectedDeferred->getViewportOutputImageShared();
        catalog.postprocessOutputImageOwner = selectedDeferred->getPostprocessOutputImageShared();
        catalog.bloomExtractOwner           = selectedDeferred->getBloomExtractImageShared();
        catalog.bloomBlurOwner              = selectedDeferred->getBloomBlurImageShared();
        catalog.bloomCompositeOwner         = selectedDeferred->getBloomCompositeImageShared();
    }

    return catalog;
}

Extent2D RenderRuntime::getViewportExtent() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getViewportExtent();
    }
    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        return Extent2D::fromVec2(_viewportRect.extent);
    }
    return {};
}

DeferredPipelineDebugViews RenderRuntime::getDeferredPipelineDebugViews() const
{
    if (auto* pipeline = getSelectedDeferredPipeline()) {
        return pipeline->buildDebugViews();
    }
    return {};
}

RenderTargetCatalog RenderRuntime::buildRenderTargetCatalog() const
{
    RenderTargetCatalog catalog{};

    if (auto presentationImage = getCurrentPresentationImageShared()) {
        catalog.entries.push_back({
            .label            = "Presentation",
            .owner            = RenderTargetCatalog::Entry::EOwner::Presentation,
            .colorFormats     = {_render->getSwapchain()->getFormat()},
            .colorAttachments = {presentationImage},
            .extent           = presentationImage->getExtent(),
            .frameBufferCount = _render->getSwapchain()->getImageCount(),
            .bSwapChainTarget = true,
            .bEditable        = false,
        });
    }
    if (auto* pipeline = getActivePipeline()) {
        pipeline->appendRenderTargetEntries(catalog);
    }

    return catalog;
}

void RenderRuntime::requestRenderTargetFormat(const RenderTargetFormatCommand& command)
{
    if (command.format == EFormat::Undefined || command.owner == RenderTargetCatalog::Entry::EOwner::Presentation) {
        return;
    }

    _pendingRenderTargetFormatCommands.push_back(command);
}

void RenderRuntime::applyPendingRenderTargetFormatCommands()
{
    if (_pendingRenderTargetFormatCommands.empty()) {
        return;
    }

    auto* pipeline = getActivePipeline();
    if (!pipeline) {
        _pendingRenderTargetFormatCommands.clear();
        return;
    }

    for (const auto& command : _pendingRenderTargetFormatCommands) {
        if (command.attachment == RenderTargetFormatCommand::EAttachment::Depth) {
            pipeline->setRenderTargetDepthFormat(command.owner, command.format);
        }
        else {
            pipeline->setRenderTargetColorFormat(command.owner, command.colorAttachmentIndex, command.format);
        }
    }
    _pendingRenderTargetFormatCommands.clear();
}

DebugRenderSystem& RenderRuntime::getDebugRenderSystem() const
{
    return DebugRenderSystem::get();
}

ForwardRenderPipeline* RenderRuntime::getSelectedForwardPipeline() const
{
    if (_renderPipeline == ERenderPipeline::Forward && _forwardPipeline) {
        return _forwardPipeline.get();
    }
    return nullptr;
}

DeferredRenderPipeline* RenderRuntime::getSelectedDeferredPipeline() const
{
    if (_renderPipeline == ERenderPipeline::Deferred && _deferredPipeline) {
        return _deferredPipeline.get();
    }
    return nullptr;
}

void RenderRuntime::initActivePipeline()
{
    int windowWidth = 0;
    int windowHeight = 0;
    _render->getWindowSize(windowWidth, windowHeight);

    if (_renderPipeline == ERenderPipeline::Forward) {
        initForwardPipeline(windowWidth, windowHeight);
    }
    else {
        initDeferredPipeline(windowWidth, windowHeight);
    }

    if (auto* pipeline = getActivePipeline()) {
        Render2D::init(_render, pipeline->getViewportColorFormat(), pipeline->getViewportDepthFormat());
    }
}

void RenderRuntime::initForwardPipeline(int windowWidth, int windowHeight)
{
    _forwardPipeline = ya::makeShared<ForwardRenderPipeline>();
    _forwardPipeline->init(ForwardRenderPipeline::InitDesc{
        .render                       = _render,
        .windowW                      = windowWidth,
        .windowH                      = windowHeight,
        .shadowSettings               = _app ? &_app->getRenderServices().getShadowSettings() : nullptr,
        .runtimeServices              = this,
    });
}

void RenderRuntime::initDeferredPipeline(int windowWidth, int windowHeight)
{
    _deferredPipeline = ya::makeShared<DeferredRenderPipeline>();
    _deferredPipeline->init(DeferredRenderPipeline::InitDesc{
        .render                   = _render,
        .windowW                  = windowWidth,
        .windowH                  = windowHeight,
        .shadowSettings           = _app ? &_app->getRenderServices().getShadowSettings() : nullptr,
        .automationShadowOverrides = _app ? &_app->getDesc().automation.shadow : nullptr,
        .environmentLightingDSL = _sharedResourceProvider.getEnvironmentLightingDescriptorSetLayout(),
        .runtimeServices          = this,
    });
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
    if (_pendingRenderPipeline == _renderPipeline && !_pendingActivePipelineReload) {
        return;
    }
    YA_PROFILE_FUNCTION_LOG();

    YA_CORE_INFO("{} render pipeline: {} -> {}",
                 _pendingActivePipelineReload ? "Reloading" : "Switching",
                 toString(_renderPipeline),
                 toString(_pendingRenderPipeline));

    shutdownActivePipeline();
    _renderPipeline = _pendingRenderPipeline;
    _pendingActivePipelineReload = false;
    initActivePipeline();

    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        onViewportResized(_viewportRect);
    }
}

} // namespace ya
