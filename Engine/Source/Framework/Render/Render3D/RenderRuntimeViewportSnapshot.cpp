#include "RHI/Core/RenderTexture.h"
#include "RenderRuntime.h"

namespace ya
{

RenderViewportSnapshot RenderRuntime::buildViewportSnapshot() const
{
    const auto debugOutputs = buildPipelineDebugOutputCatalog();

    RenderViewportSnapshot snapshot;
    snapshot.bForwardPipeline       = (_pipelineCoordinator.getRenderPipeline() == ERenderPipeline::Forward);
    snapshot.bPostprocessingEnabled = debugOutputs.bPostprocessingEnabled;
    if (const auto* output = publishedViewOutput()) {
        snapshot.viewportImageOwner = output->displayImage();
        snapshot.viewportDepthOwner = output->depth;
        snapshot.entityIdImageOwner = output->entityId;
    }
    else {
        snapshot.viewportImageOwner = getViewportDisplayImageShared();
        if (auto* pipeline = getActivePipeline()) {
            snapshot.viewportDepthOwner = pipeline->getViewportDepthImageShared();
            snapshot.entityIdImageOwner = pipeline->getEntityIdImageShared();
        }
    }
    snapshot.viewportImageView = snapshot.viewportImageOwner && snapshot.viewportImageOwner->getImageView()
                                     ? snapshot.viewportImageOwner->getImageView()
                                     : nullptr;

    ensureViewportDebugCatalog();
    snapshot.debugCatalog = _viewportDebugCatalog;
    if (snapshot.debugCatalog) {
        snapshot.debugImages.reserve(snapshot.debugCatalog->slots.size());
    }

    appendViewportDebugImages(snapshot.debugImages, nullptr);
    return snapshot;
}

} // namespace ya
