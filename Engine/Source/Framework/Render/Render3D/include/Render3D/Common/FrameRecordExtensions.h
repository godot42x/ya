#pragma once

#include "Core/Common/Types.h"
#include "Core/Base.h"
#include "Graph/RenderGraph.h"

namespace ya
{

struct ICommandBuffer;

/// What the host records into a frame's command buffer, at the points the
/// renderer defines.
///
/// The renderer owns a frame's record order: world graph, then the runtime UI
/// and View compose, then display compose onto the swapchain image. The host
/// contributes to that order -- editor chrome, module overlays, automation
/// capture -- but does not restate it. Naming the stages here keeps the order
/// in one place (the coordinator's `record()`) instead of splitting it between
/// a plan full of callbacks and the renderer that happens to invoke them.
///
/// Every stage is optional: the default is "this host contributes nothing
/// here", which is exactly what a headless or UI-only frame wants.
struct IFrameRecordExtensions
{
    virtual ~IFrameRecordExtensions() = default;

    /// After the world graph and the runtime UI / View compose, before display
    /// compose. Command recording is already active, so GPU resources must not
    /// be recreated here.
    virtual void recordViewCompose(ICommandBuffer& cmdBuf, float deltaTime)
    {
        (void)cmdBuf;
        (void)deltaTime;
    }

    /// Inside display compose, before the presentation graph is built. Content
    /// recorded here is consumed later by that graph.
    virtual void recordBeforeDisplayExtensions(ICommandBuffer& cmdBuf, float deltaTime)
    {
        (void)cmdBuf;
        (void)deltaTime;
    }

    /// The host's own passes inside display compose, recorded into the command
    /// buffer the graph build context owns.
    virtual void recordDisplayExtensions(ICommandBuffer& cmdBuf, float deltaTime)
    {
        (void)cmdBuf;
        (void)deltaTime;
    }

    /// A capture pass appended inside display compose. Returning true means one
    /// was appended, which is the graph's cue to read its output.
    [[nodiscard]] virtual bool appendDisplayCapture([[maybe_unused]] RenderGraph& graph,
                                                    [[maybe_unused]] RGTextureHandle presentationOutput,
                                                    [[maybe_unused]] Extent2D        presentationExtent)
    {
        return false;
    }
};

} // namespace ya
