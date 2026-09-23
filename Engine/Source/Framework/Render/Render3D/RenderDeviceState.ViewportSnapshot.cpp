#include "RHI/Core/RenderTexture.h"
#include "Render3D/RenderDeviceState.h"

namespace ya
{

RenderViewportSnapshot RenderDeviceState::buildViewportSnapshot(uint32_t   flightIndex,
                                                              SceneViewId viewId,
                                                              Scene*      inspectScene) const
{
    const auto debugOutputs = buildPipelineDebugOutputCatalog(flightIndex, viewId);

    RenderViewportSnapshot snapshot;
    snapshot.bForwardPipeline       = (_pipelineCoordinator.getRenderPipeline() == ERenderPipeline::Forward);
    snapshot.bPostprocessingEnabled = debugOutputs.bPostprocessingEnabled;
    if (const auto* output = getViewOutput(flightIndex, viewId)) {
        snapshot.viewportImageOwner = output->displayImage();
        snapshot.viewDepthOwner = output->depth;
        snapshot.entityIdImageOwner = output->entityId;
    }
    else {
        // No output for the named View this flight: the panel shows the
        // pipeline's own persistent targets, which hold the last frame that did
        // render. The fallback is scoped to the View that was asked about, not
        // to whichever View the pipeline happened to record last.
        if (auto* pipeline = getActivePipeline()) {
            snapshot.viewDepthOwner = pipeline->getViewDepthImageShared(viewId);
            snapshot.entityIdImageOwner = pipeline->getEntityIdImageShared(viewId);
        }
    }
    snapshot.viewportImageView = snapshot.viewportImageOwner && snapshot.viewportImageOwner->getImageView()
                                     ? snapshot.viewportImageOwner->getImageView()
                                     : nullptr;

    // One resolved input for both: the catalog (metadata, cached by digest) and
    // the images the panel uploads. Resolving here keeps the builder a pure
    // function of handles -- it never asks the renderer which pipeline ran.
    const ViewportDebugCatalogInput debugInput =
        makeViewportDebugCatalogInput(flightIndex, viewId, inspectScene);

    snapshot.debugCatalog = _viewportDebugCache.get(debugInput);
    if (snapshot.debugCatalog) {
        snapshot.debugImages.reserve(snapshot.debugCatalog->slots.size());
    }

    appendViewportDebugImages(snapshot.debugImages, nullptr, debugInput);
    return snapshot;
}

} // namespace ya
