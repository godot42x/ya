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

namespace
{

const char* toString(PipelineCoordinator::ERenderPipeline pipeline)
{
    return pipeline == PipelineCoordinator::ERenderPipeline::Forward ? "Forward" : "Deferred";
}

} // namespace

void PipelineCoordinator::init(const InitDesc& desc)
{
    YA_CORE_ASSERT(desc.render != nullptr, "PipelineCoordinator requires a render backend");

    _render                = desc.render;
    _hostServices          = desc.hostServices;
    _sharedResourceProvider = desc.sharedResourceProvider;
    _debugRenderSystem     = desc.debugRenderSystem;
    _reapplyViewRectSink   = desc.reapplyViewRectSink;
    _viewWidth         = desc.viewWidth;
    _viewHeight        = desc.viewHeight;

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
    _reapplyViewRectSink   = {};
    _viewWidth         = 0;
    _viewHeight        = 0;
    _pendingRenderTargetFormatCommands.clear();
}

void PipelineCoordinator::applyPendingChanges()
{
    applyPendingRenderPipelineSwitch();
    applyPendingRenderTargetFormatCommands();
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
    const int windowWidth  = _viewWidth;
    const int windowHeight = _viewHeight;

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

void PipelineCoordinator::applyPendingRenderPipelineSwitch()
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

    if (_reapplyViewRectSink) {
        _reapplyViewRectSink();
    }
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
