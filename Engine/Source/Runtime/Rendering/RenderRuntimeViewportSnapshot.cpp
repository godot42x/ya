#include "Render/Core/RenderImage.h"
#include "RenderRuntime.h"

namespace ya
{

std::shared_ptr<RenderImage> RenderRuntime::getViewportSnapshotImageShared() const
{
    if (auto postprocessOutput = getPostprocessOutputImageShared()) {
        return postprocessOutput;
    }
    return getActiveViewportImageShared();
}

RenderViewportSnapshot RenderRuntime::buildViewportSnapshot() const
{
    const auto debugOutputs = buildPipelineDebugOutputCatalog();

    RenderViewportSnapshot snapshot;
    snapshot.bForwardPipeline       = (_renderPipeline == ERenderPipeline::Forward);
    snapshot.bPostprocessingEnabled = debugOutputs.bPostprocessingEnabled;
    snapshot.viewportImageOwner     = getViewportSnapshotImageShared();
    snapshot.viewportImageView      = snapshot.viewportImageOwner && snapshot.viewportImageOwner->getImageView()
                                        ? snapshot.viewportImageOwner->getImageView()
                                        : nullptr;
    if (auto* pipeline = getActivePipeline()) {
        snapshot.viewportDepthOwner = pipeline->getViewportDepthImageShared();
        snapshot.entityIdImageOwner = pipeline->getEntityIdImageShared();
    }

    ensureViewportDebugCatalog();
    snapshot.debugCatalog = _viewportDebugCatalog;
    if (snapshot.debugCatalog) {
        snapshot.debugImages.reserve(snapshot.debugCatalog->slots.size());
    }

    appendViewportDebugImages(snapshot.debugImages, nullptr);
    return snapshot;
}

} // namespace ya
