#include "Render3D/Composition/Features/ShadowFeature.h"

#include "Core/Log.h"
#include "Render3D/Common/RenderSubmission.h"

namespace ya
{

ShadowFeature::Resolved ShadowFeature::resolve(const Settings& settings,
                                               const Facts&    facts,
                                               const Inputs&   inputs) const
{
    (void)facts;
    (void)inputs;

    Resolved resolved;
    resolved.bEnabled = settings.bEnabled;
    return resolved;
}

Result<ShadowFeature::Prepared, FeatureError> ShadowFeature::prepareBindings(
    const Resolved&       resolved,
    const PrepareContext& ctx) const
{
    Prepared prepared;
    if (!resolved.bEnabled) {
        return prepared;
    }
    // Settings say shadows are on but this pipeline has no shadow stage (its
    // resources were never created): that is "no shadows this frame", the same
    // answer the stage-less path always gave, not a composition failure.
    if (ctx.stage == nullptr || ctx.submission == nullptr || !ctx.view.valid()) {
        return prepared;
    }

    // The technique decides the View's shadow slot and prepares its per-View
    // frame resources; the token it returns is the only handle append needs,
    // so nothing here remembers "the current View". It writes the View's shadow
    // slot back into the context it is given, so it gets a copy: that slot
    // belongs to the shadow binding table, not to the View's own.
    RenderViewRecordingContext view = ctx.view;
    prepared.view = ctx.stage->prepareView(*ctx.submission, view);
    // An enabled shadow stage that produced no View token has nothing to draw
    // (no shadow-casting content, technique off); append turns that into no
    // passes rather than an error.
    return prepared;
}

Result<ShadowFeature::GraphOutputs, FeatureError> ShadowFeature::append(
    RenderGraph&               graph,
    Stage*                     stage,
    const FeatureGraphContext& ctx,
    const Facts&               facts,
    const Resolved&            resolved,
    const Prepared&            prepared,
    const Inputs&              inputs) const
{
    (void)facts;
    (void)inputs;

    GraphOutputs outputs;
    if (!resolved.bEnabled || stage == nullptr || ctx.stageCtx == nullptr || !prepared.isValid()) {
        return outputs;
    }

    outputs.outputs = stage->appendGraphPasses(graph, *ctx.stageCtx, prepared.view, ctx.predecessor);
    return outputs;
}

} // namespace ya
