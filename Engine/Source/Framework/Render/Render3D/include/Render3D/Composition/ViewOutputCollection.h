#pragma once

#include "Graph/RenderGraphExecutor.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Pipelines/BloomPostprocessing.h"

#include <cstdint>
#include <memory>

namespace ya::composition
{

/// The View outputs every composition publishes through shared (feature-owned)
/// exports, read in one place.
///
/// The two pipelines keep their own vocabulary for their own passes ("Deferred
/// View.Color" vs "ForwardView.Color"), because those names are the topology's
/// diagnostics. But the color-effect features are the SAME code on both maps, so
/// their export names are the same on both, and resolving them per-pipeline made
/// two collectors agree by copy. This is the single reader of those names.
struct SharedViewOutputs
{
    std::shared_ptr<RenderTexture> bloomExtract;
    std::shared_ptr<RenderTexture> bloomBlur;
    std::shared_ptr<RenderTexture> bloomComposite;
    std::shared_ptr<RenderTexture> display;
};

/// Read the shared outputs for one View out of an executed graph. Missing
/// outputs stay null: a disabled feature exports nothing, and the caller's
/// presence gates (bloom-off, finalize-off) already treat null as "not there".
[[nodiscard]] inline SharedViewOutputs collectSharedViewOutputs(
    const RenderGraphExecutionResult& result,
    uint64_t                          viewId)
{
    SharedViewOutputs outputs;

    outputs.bloomExtract = result.getExportedTextureShared(
        makeViewGraphName(BloomPostprocessing::kExtractExportName, viewId));
    // The last blur pass alternates ping-pong sides; a 4-pass chain ends on Pong,
    // an odd one on Ping. Ask for Pong first and fall back, which is what both
    // pipelines did inline.
    outputs.bloomBlur = result.getExportedTextureShared(
        makeViewGraphName(BloomPostprocessing::kBlurPongExportName, viewId));
    if (!outputs.bloomBlur) {
        outputs.bloomBlur = result.getExportedTextureShared(
            makeViewGraphName(BloomPostprocessing::kBlurPingExportName, viewId));
    }
    outputs.bloomComposite = result.getExportedTextureShared(
        makeViewGraphName(BloomPostprocessing::kOutputExportName, viewId));
    // The display image is the finalize output, always: finalize is the pass
    // that encodes the renderer's linear color, so it runs every frame and the
    // display image never falls back to the raw color attachment. A fallback
    // here is what let a presentation pass rescue an unencoded image by grading
    // it itself, which double-graded the normal path.
    outputs.display = result.getExportedTextureShared(
        makeViewGraphName(PostProcessingStage::kOutputExportName, viewId));

    return outputs;
}

} // namespace ya::composition
