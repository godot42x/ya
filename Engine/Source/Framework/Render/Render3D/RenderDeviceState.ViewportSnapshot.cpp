#include "RHI/Core/RenderTexture.h"
#include "Render3D/RenderDeviceState.h"

namespace ya
{

RenderViewportSnapshot RenderDeviceState::buildViewportSnapshot(Scene* inspectScene) const
{
    const auto debugOutputs = buildPipelineDebugOutputCatalog();

    RenderViewportSnapshot snapshot;
    snapshot.bForwardPipeline       = (_pipelineCoordinator.getRenderPipeline() == ERenderPipeline::Forward);
    snapshot.bPostprocessingEnabled = debugOutputs.bPostprocessingEnabled;
    if (const auto* output = publishedViewOutput()) {
        snapshot.viewportImageOwner = output->displayImage();
        snapshot.viewDepthOwner = output->depth;
        snapshot.entityIdImageOwner = output->entityId;
    }
    else {
        snapshot.viewportImageOwner = getViewDisplayImageShared();
        if (auto* pipeline = getActivePipeline()) {
            snapshot.viewDepthOwner = pipeline->getViewDepthImageShared();
            snapshot.entityIdImageOwner = pipeline->getEntityIdImageShared();
        }
    }
    snapshot.viewportImageView = snapshot.viewportImageOwner && snapshot.viewportImageOwner->getImageView()
                                     ? snapshot.viewportImageOwner->getImageView()
                                     : nullptr;

    // One resolved input for both: the catalog (metadata, cached by digest) and
    // the images the panel uploads. Resolving here keeps the builder a pure
    // function of handles -- it never asks the renderer which pipeline ran.
    const ViewportDebugCatalogInput debugInput = makeViewportDebugCatalogInput(inspectScene);

    snapshot.debugCatalog = _viewportDebugCache.get(debugInput);
    if (snapshot.debugCatalog) {
        snapshot.debugImages.reserve(snapshot.debugCatalog->slots.size());
    }

    appendViewportDebugImages(snapshot.debugImages, nullptr, debugInput);
    return snapshot;
}

} // namespace ya
