#include "RHI/Core/RenderTexture.h"
#include "RenderRuntime.h"

namespace ya
{

RenderViewportSnapshot RenderRuntime::buildViewportSnapshot() const
{
    const auto debugOutputs = buildPipelineDebugOutputCatalog();

    RenderViewportSnapshot snapshot;
    snapshot.bForwardPipeline       = (_renderPipeline == ERenderPipeline::Forward);
    snapshot.bPostprocessingEnabled = debugOutputs.bPostprocessingEnabled;
    snapshot.viewportImageOwner     = getViewportDisplayImageShared();
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
