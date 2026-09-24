#include "Render3D/Services/SurfacePresentation.h"

#include "Core/Log.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Render.h"

namespace ya
{

void SurfacePresentation::init(const InitDesc& desc)
{
    YA_CORE_ASSERT(desc.render != nullptr, "SurfacePresentation requires a render backend");
    YA_CORE_ASSERT(desc.present != nullptr, "SurfacePresentation requires a present surface");
    YA_CORE_ASSERT(desc.present->getSwapchain() != nullptr,
                   "SurfacePresentation requires a swapchain");

    _id      = desc.id;
    _present = desc.present;

    // Built from this surface's format, before the graph that uses it as its
    // backdrop writer: a surface whose format applies the transfer function
    // must be written by a pass that knows it, which is why the format is read
    // here instead of being handed down from the primary window.
    _writePass = ya::makeShared<SurfaceWritePass>();
    _writePass->init(SurfaceWritePass::InitDesc{
        .render        = desc.render,
        .surfaceFormat = _present->getSwapchain()->getFormat(),
    });

    _graph.init(PresentationGraphService::InitDesc{
        .render         = desc.render,
        .present        = _present,
        .backdropWriter = _writePass.get(),
    });
}

void SurfacePresentation::shutdown()
{
    _graph.shutdown();
    if (_writePass) {
        _writePass->shutdown();
        _writePass.reset();
    }
    _id      = {};
    _present = nullptr;
}

void SurfacePresentation::recordDisplayCompose(const FSurfaceImage&    backdrop,
                                               RenderSubmission&       submission,
                                               float                   deltaTime,
                                               IFrameRecordExtensions* extensions,
                                               ICommandBuffer*         cmdBuf,
                                               int32_t                 imageIndex)
{
    _graph.recordDisplayCompose(backdrop, submission, deltaTime, extensions, cmdBuf, imageIndex);
}

std::shared_ptr<RenderTexture> SurfacePresentation::currentImageShared() const
{
    return _graph.getCurrentPresentationImageShared();
}

} // namespace ya
