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

    if (_forwardPipeline) {
        _forwardPipeline->onViewportResized(rect);
    }
    if (_deferredPipeline) {
        _deferredPipeline->onViewportResized(rect);
    }
}

void RenderRuntime::renderFrame(const FrameInput& input)
{
    YA_PROFILE_SCOPE("RenderRuntime::renderFrame");
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
    renderPresentationPass(input, cmdBuf.get());
    {
        YA_PERF_SCOPE(perf::sample::renderSubmit(), perf::metric::cpuTimeMs(), perf::domain::render());
        submitFrame(imageIndex, cmdBuf.get());
    }

    endFrameCapture();
}

void RenderRuntime::runFramePrologue()
{
    applyPendingShadingModelSwitch();
    if (_app) {
        _offscreen.tick(*_app);
    }
}

void RenderRuntime::beginFrameCapture()
{
    _diagnostics.onFrameBegin();
}

void RenderRuntime::endFrameCapture()
{
    _diagnostics.onFrameEnd();
}

bool RenderRuntime::prepareFrame(const FrameInput& input, int32_t& imageIndex, std::shared_ptr<ICommandBuffer>& cmdBuf)
{
    YA_PROFILE_FUNCTION()
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
    if (_forwardPipeline) return _forwardPipeline->getPostprocessOutputTexture();
    if (_deferredPipeline) return _deferredPipeline->getPostprocessOutputTexture();
    return nullptr;
}

Texture* RenderRuntime::getBloomExtractTexture() const
{
    if (_forwardPipeline) return _forwardPipeline->getBloomExtractTexture();
    if (_deferredPipeline) return _deferredPipeline->getBloomExtractTexture();
    return nullptr;
}

Texture* RenderRuntime::getBloomBlurTexture() const
{
    if (_forwardPipeline) return _forwardPipeline->getBloomBlurTexture();
    if (_deferredPipeline) return _deferredPipeline->getBloomBlurTexture();
    return nullptr;
}

Texture* RenderRuntime::getBloomCompositeTexture() const
{
    if (_forwardPipeline) return _forwardPipeline->getBloomCompositeTexture();
    if (_deferredPipeline) return _deferredPipeline->getBloomCompositeTexture();
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
    if (_forwardPipeline) return _forwardPipeline->isPostprocessingEnabled();
    if (_deferredPipeline) return _deferredPipeline->isPostprocessingEnabled();
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
    return _diagnostics.requestAutomationRenderDocCapture();
}

bool RenderRuntime::isAutomationRenderDocCapturePending() const
{
    return _diagnostics.isAutomationRenderDocCapturePending();
}

bool RenderRuntime::isAutomationRenderDocCaptureTerminal() const
{
    return _diagnostics.isAutomationRenderDocCaptureTerminal();
}

const std::string& RenderRuntime::getAutomationRenderDocCapturePath() const
{
    return _diagnostics.getAutomationRenderDocCapturePath();
}

const std::string& RenderRuntime::getAutomationRenderDocPassSummaryPath() const
{
    return _diagnostics.getAutomationRenderDocPassSummaryPath();
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
