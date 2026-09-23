#include "GameEditor/UI/Sections/RuntimeRenderTargetSection.h"

#include "GameRuntime/App.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Text.h"
#include "Render3D/Common/RenderTargetCatalog.h"

#include <format>
#include <utility>

namespace ya
{

namespace
{

const char* ownerLabel(RenderTargetCatalog::Entry::EOwner owner)
{
    switch (owner) {
    case RenderTargetCatalog::Entry::EOwner::Presentation:
        return "Presentation";
    case RenderTargetCatalog::Entry::EOwner::ForwardView:
        return "Forward View";
    case RenderTargetCatalog::Entry::EOwner::ForwardShadow:
        return "Forward Shadow";
    case RenderTargetCatalog::Entry::EOwner::DeferredGBuffer:
        return "Deferred GBuffer";
    case RenderTargetCatalog::Entry::EOwner::DeferredView:
        return "Deferred View";
    case RenderTargetCatalog::Entry::EOwner::DeferredShadow:
        return "Deferred Shadow";
    }
    return "Unknown";
}

} // namespace

RuntimeRenderTargetSection::RuntimeRenderTargetSection(std::string name)
    : UICompoundWidget(std::move(name), "panel")
{
}

void RuntimeRenderTargetSection::construct()
{
    _summary = ui::text("RuntimeTargetsSummary").share();
    _details = ui::text("RuntimeTargetsDetails").share();

    auto rows = ui::column("RuntimeTargetsRows")
                    .setSpacing(3.0f)
                    .child(ui::text("RuntimeTargetsHeader")
                               .setText("Render Targets")
                               .setStyleKey("text.header"))
                    .child(_summary)
                    .child(_details);
    addDetachedChild(rows.release());
}

void RuntimeRenderTargetSection::sync(const App* app)
{
    if (!_summary || !_details || !app) {
        return;
    }

    const auto& renderServices = app->getRenderServices();
    if (!renderServices.hasRenderer()) {
        _summary->setText("Render targets unavailable");
        _details->setText("");
        return;
    }

    const auto catalog = renderServices.buildRenderTargetCatalog();
    _summary->setText(std::format("Targets: {}", catalog.entries.size()));
    if (catalog.entries.empty()) {
        _details->setText("No render targets are available");
        return;
    }

    std::string text;
    for (const auto& entry : catalog.entries) {
        if (!text.empty()) {
            text.push_back('\n');
        }
        // A row is one render target, so a target a View owns names that View:
        // two Views of the same kind and the same extent are otherwise the same
        // line. A target no View owns (the window's present image, a shadow
        // map) has no identity to show and keeps the kind label alone.
        const std::string targetName = entry.viewId != 0
                                           ? std::format("{} {:#x}", entry.label, entry.viewId)
                                           : std::string(entry.label);
        text += std::format("{} | {} | {}x{} | {}{}",
                            targetName,
                            ownerLabel(entry.owner),
                            entry.extent.width,
                            entry.extent.height,
                            entry.bSwapChainTarget ? "swapchain" : "offscreen",
                            entry.bEditable ? "" : " | read-only");
    }
    _details->setText(std::move(text));
}

} // namespace ya
