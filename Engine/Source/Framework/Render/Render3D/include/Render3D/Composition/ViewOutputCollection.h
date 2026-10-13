#pragma once

#include "Graph/RenderGraphExecutor.h"
#include "Render3D/Common/RenderViewOutput.h"

#include <cstdint>
#include <memory>

namespace ya::composition
{

/// The one GraphOutputs → RenderViewOutput mapping (D4).
///
/// Every handle is read back typed, by the same RGTextureHandle the feature
/// reported in its GraphOutputs -- never through an export name. Export names
/// stay on the graph as topology diagnostics only. Two properties follow:
/// - The ask and the answer are the same identity: a handle that was never
///   exported answers null, and a handle the feature reported is read back
///   without a string in between that could drift from it (D4 found Forward's
///   depth handle was never exported at all, so its published depth is null;
///   under names that read as "the lookup contract", under handles it reads as
///   "this producer registered nothing", which is the truth).
/// - Asking for the wrong side of a ping-pong chain is unspellable: the
///   feature reports the chain's own final handle, so the old Pong-first
///   fallback (which could answer with a midpoint blur when the pass count was
///   odd) has no equivalent.
///
/// Each recipe adapts its own GraphOutputs into the small descriptor below;
/// the mapping itself is single and shared.
struct ViewOutputHandles
{
    RGTextureHandle color{};
    RGTextureHandle depth{};
    RGTextureHandle entityId{};
    RGTextureHandle display{};
    RGTextureHandle bloomExtract{};
    RGTextureHandle bloomBlur{};
    RGTextureHandle bloomComposite{};
};

/// Resolve one View's published output from the executed graph. Handles that
/// were not exported (disabled feature) stay null; that is the same presence
/// contract the name-based path had, now keyed by identity instead of string.
[[nodiscard]] inline RenderViewOutput collectViewOutput(const RenderGraphExecutionResult& result,
                                                        const ViewOutputHandles&          handles,
                                                        const RenderViewOutputDesc&       declaredDesc)
{
    RenderViewOutput output;
    output.desc    = declaredDesc;
    output.color    = result.getExportedTextureShared(handles.color);
    output.depth    = result.getExportedTextureShared(handles.depth);
    output.entityId = result.getExportedTextureShared(handles.entityId);
    output.display  = result.getExportedTextureShared(handles.display);
    output.bloomExtract   = result.getExportedTextureShared(handles.bloomExtract);
    output.bloomBlur      = result.getExportedTextureShared(handles.bloomBlur);
    output.bloomComposite = result.getExportedTextureShared(handles.bloomComposite);

    if (output.color) {
        output.desc.colorFormat = output.color->getFormat();
        if (!output.desc.hasExtent()) {
            output.desc.extent = output.color->getExtent();
        }
    }
    if (output.depth) {
        output.desc.depthFormat = output.depth->getFormat();
    }
    return output;
}

} // namespace ya::composition
