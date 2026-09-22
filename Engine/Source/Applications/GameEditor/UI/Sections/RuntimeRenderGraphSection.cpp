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
    _pipeline = ui::text("RuntimeGraphPipeline").share();
    _passes = ui::text("RuntimeGraphPasses").share();
    _dependencies = ui::text("RuntimeGraphDependencies").share();
    _status = ui::text("RuntimeGraphStatus").setStyleKey("text.muted").share();
    auto rows = ui::column("RuntimeGraphRows").setSpacing(4.0f)
        .child(ui::text("RuntimeGraphHeader").setText("Render Graph").setStyleKey("text.header"))
        .child(_pipeline).child(_passes).child(_dependencies).child(_status);
    addDetachedChild(rows.release());
}
void RuntimeRenderGraphSection::sync(const App* app)
{
    if (!_pipeline || !_passes || !_dependencies || !_status || !app) return;
    const auto& renderServices = app->getRenderServices();
    if (!renderServices.hasRenderer()) { _status->setText("Frame graph unavailable"); return; }
    // Which strategy is active, and what graph it compiled, are asked of the
    // renderer: the panel states what it wants to show instead of downcasting
    // into a concrete pipeline to find out what it is holding.
    const char* name = toString(renderServices.getRenderPipelineKind());
    const RGTopologyDescription* topology = renderServices.getFrameGraphTopology();
    _pipeline->setText(std::format("Pipeline: {}", name));
    if (!topology) { _passes->setText("Passes: <unavailable>"); _dependencies->setText("Dependencies: <unavailable>"); _status->setText("No active frame graph"); return; }
    _passes->setText(std::format("Passes: {}", topology->passOrder.size()));
    _dependencies->setText(std::format("Dependencies: {}", topology->dependencies.size()));
    _status->setText(topology->passOrder.empty() ? "No compiled frame graph captured yet" : "Compiled frame graph available");
}
}
