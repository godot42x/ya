#include "RenderRuntime.h"

#include "App.h"
#include "DebugRenderSystem.h"
#include "Core/Async/TaskQueue.h"
#include "Core/Debug/PerfKeys.h"
#include "Core/Debug/PerfState.h"
#include "Core/Debug/RenderDocCapture.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/2D/Render2D.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

namespace ya
{

void RenderRuntime::onViewportResized(Rect2D rect)
{
    _viewportRect = rect;

    if (_forwardPipeline) {
        _forwardPipeline->onViewportResized(rect);
    }
    if (_deferredPipeline) {
        _deferredPipeline->onViewportResized(rect);
    }
}

void RenderRuntime::finalizeCompletedOffscreenJobs()
{
    ya::finalizeSubmittedOffscreenJobs(_submittedOffscreenJobs);
}

void RenderRuntime::offScreenRender()
{
    if (!_render || !_app || !_offscreenCmdBuf) {
        return;
    }

    if (_offscreenPending && _offscreenFence) {
        auto*   vkRender = static_cast<VulkanRender*>(_render);
        VkFence fence    = static_cast<VkFence>(_offscreenFence);
        vkWaitForFences(vkRender->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(vkRender->getDevice(), 1, &fence);
        _offscreenPending = false;
        finalizeCompletedOffscreenJobs();
    }

    if (!_app->taskManager.hasOffscreenTasks()) {
        return;
    }

    auto cmdBuf = _offscreenCmdBuf;
    cmdBuf->reset();
    if (!cmdBuf->begin()) {
        YA_CORE_ERROR("Failed to begin offscreen command buffer");
        return;
    }

    _submittedOffscreenJobs.clear();
    _app->taskManager.updateOffscreenTasks(cmdBuf.get(), &_submittedOffscreenJobs);

    if (!cmdBuf->end()) {
        YA_CORE_ERROR("Failed to end offscreen command buffer");
        return;
    }

    _render->submitToQueue({cmdBuf->getHandle()}, {}, {}, _offscreenFence);
    _offscreenPending = true;
}

void RenderRuntime::renderFrame(const FrameInput& input)
{
    YA_PROFILE_FUNCTION()
    YA_PERF_SCOPE(perf::sample::renderRuntime(), perf::metric::cpuTimeMs(), perf::domain::render());

    runFramePrologue();
    beginFrameCapture();

    int32_t                         imageIndex = -1;
    std::shared_ptr<ICommandBuffer> cmdBuf;
    if (!prepareFrame(input, imageIndex, cmdBuf)) {
        endFrameCapture();
        return;
    }

    {
        YA_PERF_SCOPE(perf::sample::renderWorld(), perf::metric::cpuTimeMs(), perf::domain::render());
        renderWorldFrame(input, cmdBuf.get());
    }
    syncEditorFrame(input);
    {
        YA_PERF_SCOPE(perf::sample::renderPresentation(), perf::metric::cpuTimeMs(), perf::domain::render());
        renderPresentationPass(input, cmdBuf.get());
    }
    {
        YA_PERF_SCOPE(perf::sample::renderFlushCallbacks(), perf::metric::cpuTimeMs(), perf::domain::render());
        flushMainThreadCallbacks();
    }
    {
        YA_PERF_SCOPE(perf::sample::renderSubmit(), perf::metric::cpuTimeMs(), perf::domain::render());
        submitFrame(imageIndex, cmdBuf.get());
    }

    endFrameCapture();
}

void RenderRuntime::runFramePrologue()
{
    applyPendingShadingModelSwitch();
    offScreenRender();
}

void RenderRuntime::beginFrameCapture()
{
    if (_renderDoc.capture) {
        _renderDoc.capture->onFrameBegin();
    }
}

void RenderRuntime::endFrameCapture()
{
    if (_renderDoc.capture) {
        _renderDoc.capture->onFrameEnd();
    }
}

bool RenderRuntime::prepareFrame(const FrameInput& input, int32_t& imageIndex, std::shared_ptr<ICommandBuffer>& cmdBuf)
{
    ensureViewportRectInitialized(input);
    _viewportFrameBufferScale = input.viewportFrameBufferScale;
    return beginFrameCommandBuffer(imageIndex, cmdBuf);
}

void RenderRuntime::renderWorldFrame(const FrameInput& input, ICommandBuffer* cmdBuf)
{
    beginViewportPassAndTickPipeline(input, cmdBuf);
    renderViewportPassOverlays(input, cmdBuf);
    endViewportPass(cmdBuf);
}

void RenderRuntime::syncEditorFrame(const FrameInput& input)
{
    updateEditorViewportContext(input);
}

void RenderRuntime::flushMainThreadCallbacks()
{
    TaskQueue::get().processMainThreadCallbacks();
}

ForwardRenderPipeline* RenderRuntime::getForwardPipeline() const
{
    return _forwardPipeline.get();
}

bool RenderRuntime::isShadowMappingEnabled() const
{
    if (_shadingModel == EShadingModel::Forward && _forwardPipeline) return _forwardPipeline->isShadowMappingEnabled();
    if (_shadingModel == EShadingModel::Deferred && _deferredPipeline) return _deferredPipeline->isShadowMappingEnabled();
    if (_forwardPipeline) return _forwardPipeline->isShadowMappingEnabled();
    if (_deferredPipeline) return _deferredPipeline->isShadowMappingEnabled();
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
    if (_shadingModel == EShadingModel::Forward && _forwardPipeline) return _forwardPipeline->getShadowDepthRT();
    if (_shadingModel == EShadingModel::Deferred && _deferredPipeline) return _deferredPipeline->getShadowDepthRT();
    if (_forwardPipeline) return _forwardPipeline->getShadowDepthRT();
    if (_deferredPipeline) return _deferredPipeline->getShadowDepthRT();
    return nullptr;
}

IImageView* RenderRuntime::getShadowDirectionalDepthIV() const
{
    if (_shadingModel == EShadingModel::Forward && _forwardPipeline) return _forwardPipeline->getShadowDirectionalDepthIV();
    if (_shadingModel == EShadingModel::Deferred && _deferredPipeline) return _deferredPipeline->getShadowDirectionalDepthIV();
    if (_forwardPipeline) return _forwardPipeline->getShadowDirectionalDepthIV();
    if (_deferredPipeline) return _deferredPipeline->getShadowDirectionalDepthIV();
    return nullptr;
}

IImageView* RenderRuntime::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (_shadingModel == EShadingModel::Forward && _forwardPipeline) {
        return _forwardPipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    if (_shadingModel == EShadingModel::Deferred && _deferredPipeline) {
        return _deferredPipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    if (_forwardPipeline) {
        return _forwardPipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    if (_deferredPipeline) {
        return _deferredPipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    return nullptr;
}

Texture* RenderRuntime::getPostprocessOutputTexture() const
{
    if (_forwardPipeline) return _forwardPipeline->_postProcessStage.getOutputTexture();
    if (_deferredPipeline) return _deferredPipeline->_postProcessStage.getOutputTexture();
    return nullptr;
}

bool RenderRuntime::isPostprocessingEnabled() const
{
    if (_forwardPipeline) return _forwardPipeline->_postProcessStage.isEnabled();
    if (_deferredPipeline) return _deferredPipeline->_postProcessStage.isEnabled();
    return false;
}

Extent2D RenderRuntime::getViewportExtent() const
{
    if (_forwardPipeline) {
        return _forwardPipeline->getViewportExtent();
    }
    if (_deferredPipeline) {
        return _deferredPipeline->getViewportExtent();
    }
    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        return Extent2D::fromVec2(_viewportRect.extent);
    }
    return {};
}

DebugRenderSystem& RenderRuntime::getDebugRenderSystem() const
{
    return DebugRenderSystem::get();
}

bool RenderRuntime::requestAutomationRenderDocCapture()
{
    _renderDoc.bAutomationCaptureFinished = false;
    _renderDoc.bAutomationCaptureFailed   = false;

    if (!_renderDoc.capture) {
        YA_CORE_WARN("Automation requested RenderDoc capture but RenderDoc integration is disabled");
        _renderDoc.bAutomationCaptureFailed = true;
        return false;
    }

    if (!_renderDoc.capture->isAvailable()) {
        YA_CORE_WARN("Automation requested RenderDoc capture but RenderDoc is unavailable: {}",
                     _renderDoc.configuredDllPath.empty() ? "renderdoc.dll" : _renderDoc.configuredDllPath);
        _renderDoc.bAutomationCaptureFailed = true;
        return false;
    }

    if (!_renderDoc.capture->isCaptureEnabled()) {
        YA_CORE_WARN("Automation requested RenderDoc capture but capture is disabled");
        _renderDoc.bAutomationCaptureFailed = true;
        return false;
    }

    if (_renderDoc.bAutomationCaptureRequested || _renderDoc.capture->isCapturing()) {
        return true;
    }

    _renderDoc.bAutomationCaptureRequested = true;
    _renderDoc.capture->requestNextFrame();
    YA_CORE_INFO("Automation queued a single RenderDoc frame capture");
    return true;
}

bool RenderRuntime::isAutomationRenderDocCapturePending() const
{
    return _renderDoc.bAutomationCaptureRequested || (_renderDoc.capture && _renderDoc.capture->isCapturing());
}

bool RenderRuntime::isAutomationRenderDocCaptureTerminal() const
{
    return _renderDoc.bAutomationCaptureFinished || _renderDoc.bAutomationCaptureFailed;
}

void RenderRuntime::initActivePipeline()
{
    int winW = 0;
    int winH = 0;
    _render->getWindowSize(winW, winH);

    if (_shadingModel == EShadingModel::Forward) {
        _forwardPipeline = ya::makeShared<ForwardRenderPipeline>();
        _forwardPipeline->init(ForwardRenderPipeline::InitDesc{
            .render  = _render,
            .windowW = winW,
            .windowH = winH,
        });
    }
    else {
        _deferredPipeline = ya::makeShared<DeferredRenderPipeline>();
        _deferredPipeline->init(DeferredRenderPipeline::InitDesc{
            .render  = _render,
            .windowW = winW,
            .windowH = winH,
        });
    }

    if (_shadingModel == EShadingModel::Forward) {
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

void RenderRuntime::applyPendingShadingModelSwitch()
{
    if (_pendingShadingModel == _shadingModel) {
        return;
    }
    YA_PROFILE_FUNCTION_LOG();

    if ((_forwardPipeline && _forwardPipeline->hasOpenViewportPass()) ||
        (_deferredPipeline && _deferredPipeline->hasOpenViewportPass())) {
        YA_CORE_WARN("Skipping shading-model switch while a viewport pass is still open");
        return;
    }

    YA_CORE_INFO("Switching shading model: {} -> {}",
                 _shadingModel == EShadingModel::Forward ? "Forward" : "Deferred",
                 _pendingShadingModel == EShadingModel::Forward ? "Forward" : "Deferred");

    _render->waitIdle();

    shutdownActivePipeline();
    _shadingModel = _pendingShadingModel;
    initActivePipeline();

    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        onViewportResized(_viewportRect);
    }
}

} // namespace ya
