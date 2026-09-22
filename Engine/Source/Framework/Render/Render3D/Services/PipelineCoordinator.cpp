#include "Render3D/Services/PipelineCoordinator.h"

#include "Render3D/Common/RenderRuntimeHostServices.h"
#include "Render3D/Services/RenderSharedResourceProvider.h"
#include "Render2D/Render2D.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "Core/Profiling/Instrumentor.h"
#include "RHI/Render.h"

namespace ya
{

// `toString(ERenderPipelineKind)` is the shared spelling of this name; the
// coordinator has no private one to keep in step with it.

void PipelineCoordinator::init(const InitDesc& desc)
{
    YA_CORE_ASSERT(desc.render != nullptr, "PipelineCoordinator requires a render backend");

    _render                = desc.render;
    _hostServices          = desc.hostServices;
    _sharedResourceProvider = desc.sharedResourceProvider;
    _debugRenderSystem     = desc.debugRenderSystem;
    _initialViewWidth      = desc.initialViewWidth;
    _initialViewHeight     = desc.initialViewHeight;

    initActivePipeline();
}

void PipelineCoordinator::shutdown()
{
    shutdownActivePipeline();
    if (Render2D::isInitialized()) {
        Render2D::destroy();
    }
    _render                = nullptr;
    _hostServices          = nullptr;
    _sharedResourceProvider = nullptr;
    _debugRenderSystem     = nullptr;
    _initialViewWidth      = 0;
    _initialViewHeight     = 0;
    _appliedViewRect       = Rect2D{};
    _pendingRenderTargetFormatCommands.clear();
}

bool PipelineCoordinator::applyPendingChanges(Rect2D viewRect)
{
    // The rect this call sizes pipelines with. A degenerate rect is not "sized
    // zero": it is "no View this frame", and the already applied rect stands.
    const bool bDescribesPixels = viewRect.extent.x > 0.0f && viewRect.extent.y > 0.0f;
    const bool bRectChanged     = bDescribesPixels &&
                              (_appliedViewRect.pos.x != viewRect.pos.x ||
                               _appliedViewRect.pos.y != viewRect.pos.y ||
                               _appliedViewRect.extent.x != viewRect.extent.x ||
                               _appliedViewRect.extent.y != viewRect.extent.y);
    if (bDescribesPixels) {
        _appliedViewRect = viewRect;
    }

    const bool bRebuilt = applyPendingRenderPipelineSwitch();
    applyPendingRenderTargetFormatCommands();

    // A rebuilt pipeline is sized at the init seed, so it needs the rect even
    // when the rect did not change; an unchanged, unrebuilt one needs nothing.
    IRenderPipeline* pipeline = getActivePipeline();
    if (pipeline && (bRebuilt || bRectChanged)) {
        pipeline->onViewResized(_appliedViewRect);
    }
    return bRebuilt;
}

IRenderPipeline* PipelineCoordinator::getActivePipeline() const
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

ForwardRenderPipeline* PipelineCoordinator::getSelectedForwardPipeline() const
{
    if (_renderPipeline == ERenderPipeline::Forward && _forwardPipeline) {
        return _forwardPipeline.get();
    }
    return nullptr;
}

DeferredRenderPipeline* PipelineCoordinator::getSelectedDeferredPipeline() const
{
    if (_renderPipeline == ERenderPipeline::Deferred && _deferredPipeline) {
        return _deferredPipeline.get();
    }
    return nullptr;
}

void PipelineCoordinator::initActivePipeline()
{
    // The first build uses the init seed; the first frame's View rect arrives
    // through applyPendingChanges and resizes it.
    const int windowWidth  = _initialViewWidth;
    const int windowHeight = _initialViewHeight;

    if (_renderPipeline == ERenderPipeline::Forward) {
        initForwardPipeline(windowWidth, windowHeight);
    }
    else {
        initDeferredPipeline(windowWidth, windowHeight);
    }

    // Render2D is device-lifetime. Switching Deferred/Forward must not
    // destroy it: editor compose/chrome already hold pass slots, and
    // preparePassPipeline recreates format-specific variants.
    if (auto* pipeline = getActivePipeline(); pipeline && !Render2D::isInitialized()) {
        Render2D::init(_render, pipeline->getViewColorFormat(), pipeline->getViewDepthFormat());
    }
}

void PipelineCoordinator::initForwardPipeline(int viewWidth, int viewHeight)
{
    _forwardPipeline = ya::makeShared<ForwardRenderPipeline>();
    _forwardPipeline->init(ForwardRenderPipeline::InitDesc{
        .render          = _render,
        .viewWidth         = viewWidth,
        .viewHeight         = viewHeight,
        .shadowSettings  = _hostServices ? _hostServices->getShadowSettings() : nullptr,
    });
}

void PipelineCoordinator::initDeferredPipeline(int viewWidth, int viewHeight)
{
    _deferredPipeline = ya::makeShared<DeferredRenderPipeline>();
    _deferredPipeline->init(DeferredRenderPipeline::InitDesc{
        .render                    = _render,
        .viewWidth                   = viewWidth,
        .viewHeight                   = viewHeight,
        .shadowSettings            = _hostServices ? _hostServices->getShadowSettings() : nullptr,
        .automationShadowOverrides = _hostServices ? _hostServices->getAutomationShadowOverrides() : nullptr,
        .environmentLightingDSL    = _sharedResourceProvider
                                         ? _sharedResourceProvider->getEnvironmentLightingDescriptorSetLayout()
                                         : nullptr,
        .debugRenderSystem         = _debugRenderSystem,
    });
}

void PipelineCoordinator::shutdownActivePipeline()
{
    if (_forwardPipeline) {
        _forwardPipeline->shutdown();
        _forwardPipeline.reset();
    }
    if (_deferredPipeline) {
        _deferredPipeline->shutdown();
        _deferredPipeline.reset();
    }
}

bool PipelineCoordinator::applyPendingRenderPipelineSwitch()
{
    if (_pendingRenderPipeline == _renderPipeline && !_pendingActivePipelineReload) {
        return false;
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
    return true;
}

void PipelineCoordinator::requestRenderTargetFormat(const RenderTargetFormatCommand& command)
{
    if (command.format == EFormat::Undefined || command.owner == RenderTargetCatalog::Entry::EOwner::Presentation) {
        return;
    }

    _pendingRenderTargetFormatCommands.push_back(command);
}

void PipelineCoordinator::applyPendingRenderTargetFormatCommands()
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

} // namespace ya
