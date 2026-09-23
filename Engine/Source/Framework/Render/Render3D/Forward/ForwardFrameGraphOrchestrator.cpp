#include "Render3D/Forward/ForwardFrameGraphOrchestrator.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/Shadow/ShadowStage.h"
#include "Render3D/Forward/ForwardFrameGraphPasses.h"

namespace ya
{

void ForwardFrameGraphOrchestrator::build(const BuildDependencies& deps, const BuildInputs& inputs) const
{
    YA_CORE_ASSERT(inputs.graph != nullptr, "ForwardFrameGraphOrchestrator requires a render graph");
    YA_CORE_ASSERT(inputs.stageCtx != nullptr, "ForwardFrameGraphOrchestrator requires a stage context");
    YA_CORE_ASSERT(inputs.viewRTSpec != nullptr, "ForwardFrameGraphOrchestrator requires a view render target spec");
    YA_CORE_ASSERT(inputs.postContext != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess context");
    YA_CORE_ASSERT(deps.viewStage != nullptr, "ForwardFrameGraphOrchestrator requires a viewport stage");
    YA_CORE_ASSERT(deps.entityIdPass != nullptr, "ForwardFrameGraphOrchestrator requires an entity-id pass");
    YA_CORE_ASSERT(deps.postProcessStage != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess stage");

    auto& graph = *inputs.graph;

    ShadowGraphOutputs shadowOutputs;
    if (deps.shadowStage && inputs.bEnableShadow) {
        shadowOutputs = deps.shadowStage->appendGraphPasses(
            graph, *inputs.stageCtx, inputs.shadowPrepared, inputs.familyPredecessor);
    }

    const auto graphResources = forward_frame_graph::createViewResources(
        graph,
        *inputs.viewRTSpec,
        shadowOutputs.shadowDepth,
        inputs.viewId,
        *inputs.targets);
    const forward_frame_graph::Dependencies passDeps{
        .viewStage    = deps.viewStage,
        .entityIdPass     = deps.entityIdPass,
        .postProcessStage = deps.postProcessStage,
    };

    forward_frame_graph::appendViewPasses(graph, passDeps, inputs, graphResources);
    forward_frame_graph::appendPostprocessPasses(graph, passDeps, inputs, graphResources);
    forward_frame_graph::exportGraphOutputs(graph, graphResources, inputs.viewId);
}

} // namespace ya
