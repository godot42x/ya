#include "GameEditor/UI/RuntimeRenderGraphSection.h"
#include "GameRuntime/App.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Text.h"
#include "Render3D/RenderRuntime.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"

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
    auto* runtime = app->getRenderServices().getRenderRuntime();
    if (!runtime) { _status->setText("Frame graph unavailable"); return; }
    const auto* active = runtime->getActivePipeline();
    const RGTopologyDescription* topology = nullptr;
    const char* name = "Unknown";
    if (auto* deferred = dynamic_cast<const DeferredRenderPipeline*>(active)) { topology = &deferred->getLastFrameGraphTopology(); name = "Deferred"; }
    else if (auto* forward = dynamic_cast<const ForwardRenderPipeline*>(active)) { topology = &forward->getLastFrameGraphTopology(); name = "Forward"; }
    _pipeline->setText(std::format("Pipeline: {}", name));
    if (!topology) { _passes->setText("Passes: <unavailable>"); _dependencies->setText("Dependencies: <unavailable>"); _status->setText("No active frame graph"); return; }
    _passes->setText(std::format("Passes: {}", topology->passOrder.size()));
    _dependencies->setText(std::format("Dependencies: {}", topology->dependencies.size()));
    _status->setText(topology->passOrder.empty() ? "No compiled frame graph captured yet" : "Compiled frame graph available");
}
}
