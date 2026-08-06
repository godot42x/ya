#include "ForwardFrameGraphOrchestrator.h"

#include "Render/Core/Graph/RenderGraphImportUtils.h"
#include "Runtime/Rendering/Common/Shadow/ShadowStage.h"
#include "Runtime/Rendering/Forward/ForwardFrameGraphPasses.h"

namespace ya
{

void ForwardFrameGraphOrchestrator::build(const BuildDependencies& deps, const BuildInputs& inputs) const
{
    YA_CORE_ASSERT(inputs.graph != nullptr, "ForwardFrameGraphOrchestrator requires a render graph");
    YA_CORE_ASSERT(inputs.stageCtx != nullptr, "ForwardFrameGraphOrchestrator requires a stage context");
    YA_CORE_ASSERT(inputs.viewportRTSpec != nullptr, "ForwardFrameGraphOrchestrator requires a viewport render target spec");
    YA_CORE_ASSERT(inputs.postContext != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess context");
    YA_CORE_ASSERT(deps.viewportStage != nullptr, "ForwardFrameGraphOrchestrator requires a viewport stage");
    YA_CORE_ASSERT(deps.entityIdPass != nullptr, "ForwardFrameGraphOrchestrator requires an entity-id pass");
    YA_CORE_ASSERT(deps.postProcessStage != nullptr, "ForwardFrameGraphOrchestrator requires a postprocess stage");

    auto& graph = *inputs.graph;

    ShadowGraphOutputs shadowOutputs;
    if (deps.shadowStage && inputs.bEnableShadow) {
        shadowOutputs = deps.shadowStage->appendGraphPasses(graph, *inputs.stageCtx);
    }

    const auto graphResources = forward_frame_graph::createViewportResources(
        graph,
        *inputs.viewportRTSpec,
        shadowOutputs.shadowDepth);
    const forward_frame_graph::Dependencies passDeps{
        .viewportStage    = deps.viewportStage,
        .entityIdPass     = deps.entityIdPass,
        .postProcessStage = deps.postProcessStage,
    };

    forward_frame_graph::appendViewportPasses(graph, passDeps, inputs, graphResources);
    forward_frame_graph::appendPostprocessPasses(graph, passDeps, inputs, graphResources);
    forward_frame_graph::exportGraphOutputs(graph, graphResources);
}

} // namespace ya
