#include "GameEditor/UI/Sections/RuntimeRenderGraphSection.h"
#include "GameRuntime/App.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Text.h"
#include "Graph/RenderGraph.h"

#include <format>

namespace ya
{
RuntimeRenderGraphSection::RuntimeRenderGraphSection(std::string name) : UICompoundWidget(std::move(name), "panel") {}
void RuntimeRenderGraphSection::construct()
{
    _pipeline = makeStatRow("RuntimeGraphPipeline", "Pipeline");
    _passes = makeStatRow("RuntimeGraphPasses", "Passes");
    _dependencies = makeStatRow("RuntimeGraphDependencies", "Dependencies");
    _status = ui::text("RuntimeGraphStatus").setStyleKey("text.muted").share();
    auto rows = ui::column("RuntimeGraphRows").setSpacing(editor_density::kRowSpacing)
        .child(_pipeline.root).child(_passes.root).child(_dependencies.root).child(_status);
    addDetachedChild(rows.release());
}
void RuntimeRenderGraphSection::sync(const App* app)
{
    if (!app) return;
    const auto& renderServices = app->getRenderServices();
    if (!renderServices.hasRenderer()) { _status->setText("Frame graph unavailable"); _pipeline.value->setText("");
        _passes.value->setText(""); _dependencies.value->setText(""); return; }
    // Which strategy is active, and what graph it compiled, are asked of the
    // renderer: the panel states what it wants to show instead of downcasting
    // into a concrete pipeline to find out what it is holding.
    const char* name = toString(renderServices.getRenderPipelineKind());
    const std::vector<RGTopologyDescription>* topologies = renderServices.getFrameGraphTopologies();
    _pipeline.value->setText(name);
    if (!topologies || topologies->empty()) { _passes.value->setText("<unavailable>"); _dependencies.value->setText("<unavailable>"); _status->setText("No active frame graph"); return; }
    size_t totalPasses       = 0;
    size_t totalDependencies = 0;
    for (const RGTopologyDescription& topology : *topologies) {
        totalPasses += topology.passOrder.size();
        totalDependencies += topology.dependencies.size();
    }
    _passes.value->setText(std::format("{}", totalPasses));
    _dependencies.value->setText(std::format("{}", totalDependencies));
    _status->setText(std::format("Family graphs: {}", topologies->size()));
}
}
