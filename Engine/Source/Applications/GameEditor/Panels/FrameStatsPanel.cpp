#include "FrameStatsPanel.h"

#include "GUI/Compose/GUIRenderSurface.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/Container.h"

#include "GameRuntime/App.h"
#include "RHI/Render.h"
#include "RHI/Core/CommandBuffer.h"

#include <imgui.h>

namespace ya
{

FrameStatsPanel::FrameStatsPanel(EditorLayer* owner) : _owner(owner)
{
}

void FrameStatsPanel::ensureTree()
{
    if (_tree)
        return;

    _tree = std::make_unique<WidgetTree>();
    _tree->setLogicalExtent(_logicalExtent);

    UIElement* layer = _tree->getLayer(WidgetTree::ELayer::Content);

    auto column = std::make_shared<UIContainer>("frame-stats-column");
    column->setDirection(EWidgetBoxLayout::Vertical);
    _tree->attach(*layer, column);

    _frameIndexText = std::make_shared<UIText>("frame-index");
    _deltaText      = std::make_shared<UIText>("delta");
    _fpsText        = std::make_shared<UIText>("fps");
    _avgFpsText     = std::make_shared<UIText>("avg-fps");

    _tree->attach(*column, _frameIndexText);
    _tree->attach(*column, _deltaText);
    _tree->attach(*column, _fpsText);
    _tree->attach(*column, _avgFpsText);
}

void FrameStatsPanel::updateStats(const App& app, float dt)
{
    _fpsHistory[_historyHead] = dt > 0.0f ? 1.0f / dt : 0.0f;
    _historyHead              = (_historyHead + 1) % kHistorySize;
    if (_historyFill < kHistorySize)
        ++_historyFill;

    float sum = 0.0f;
    for (size_t i = 0; i < _historyFill; ++i)
        sum += _fpsHistory[i];
    float avg = _historyFill > 0 ? sum / static_cast<float>(_historyFill) : 0.0f;

    if (_frameIndexText) _frameIndexText->setText("Frame Index: " + std::to_string(app.getFrameIndex()));
    if (_deltaText)      _deltaText->setText("Delta Time: " + std::to_string(dt) + "s");
    if (_fpsText)        _fpsText->setText("FPS: " + std::to_string(_fpsHistory[(_historyHead + kHistorySize - 1) % kHistorySize]));
    if (_avgFpsText)     _avgFpsText->setText("Avg FPS: " + std::to_string(avg));
}

UIFrameSnapshot FrameStatsPanel::buildSnapshot()
{
    UIFrameBuildContext ctx;
    ctx.uiScale = {1.0f, 1.0f};
    ctx.offset  = {0.0f, 0.0f};
    return _tree->buildSnapshot(ctx);
}

void FrameStatsPanel::onImGuiRender(const App& app, float dt)
{
    ImGui::Begin("Frame Stats");
    updateStats(app, dt);
    ensureTree();

    // Drive the offscreen surface size from the ImGui content region so the
    // composed texture matches the window. Compose happens later in the editor
    // module's presentation pass, using this extent.
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const uint32_t w = std::max<uint32_t>(static_cast<uint32_t>(region.x), 1);
    const uint32_t h = std::max<uint32_t>(static_cast<uint32_t>(region.y), 1);
    if (_logicalExtent.width != w || _logicalExtent.height != h)
    {
        _logicalExtent = {w, h};
        if (_tree)
            _tree->setLogicalExtent(_logicalExtent);
        _surface.reset(); // recreate at the new size on next compose
    }

    if (_displayImage)
        ImGui::Image(_displayImage.get(), ImVec2(static_cast<float>(_logicalExtent.width), static_cast<float>(_logicalExtent.height)));
    ImGui::End();
}

void FrameStatsPanel::ensureTarget(IRender& render, const Extent2D& extent)
{
    if (_surface &&
        _surface->isValid() &&
        _surface->getRenderImage()->getWidth() == extent.width &&
        _surface->getRenderImage()->getHeight() == extent.height &&
        _surface->getRenderImage()->getFormat() == EFormat::R16G16B16A16_SFLOAT) {
        return;
    }
    _surface = GUIRenderSurface::createOffscreen(
        *render.getResourceFactory(),
        FGUIRenderSurfaceDesc{
            .label       = "EditorFrameStats",
            .extent      = extent,
            .colorFormat = EFormat::R16G16B16A16_SFLOAT,
        });
}

void FrameStatsPanel::compose(IRender& render, ICommandBuffer& commandBuffer)
{
    if (!_tree || !hasRenderableExtent())
        return;

    ensureTarget(render, _logicalExtent);
    if (!_surface || !_surface->isValid())
        return;

    const UIFrameSnapshot snapshot = buildSnapshot();
    const FRender2DComposePassDesc passDesc{
        .kind                  = ERender2DComposePassKind::EditorToolSurface,
        .logicalViewportExtent = _logicalExtent,
    };
    _surface->prepare(passDesc);
    _surface->record(&commandBuffer, nullptr, &snapshot, passDesc);
    _displayImage = _surface->getRenderImage();
}

} // namespace ya
