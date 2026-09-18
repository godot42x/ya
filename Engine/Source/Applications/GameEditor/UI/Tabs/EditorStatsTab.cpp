#include "GameEditor/UI/Tabs/EditorStatsTab.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GameEditor/EditorLayer.h"
#include "GameRuntime/App.h"
#include "GameEditor/UI/Shell/EditorTheme.h"

#include <format>
#include <glm/glm.hpp>

namespace ya
{

EditorStatsTab::EditorStatsTab(EditorLayer& layer)
    : UICompoundWidget("FrameStatsPanel", "panel.canvas")
    , _layer(&layer)
{
    enableTick();
}

void EditorStatsTab::construct()
{
    auto statsText = ui::text("FrameStatsBody").setText("Frame Stats").setStyleKey(editorStyle(StyleKey::Text));
    _statsText = statsText.share();
    addDetachedChild(ui::border("FrameStatsHost")
                         .setStyleKey("panel.canvas")
                         .setPadding(FMargin::all(12.0f))
                         .child(std::move(statsText), ui::contentSlot().fill())
                         .release());
}

void EditorStatsTab::tick(float deltaSeconds)
{
    App* app = App::get();
    const float fps = deltaSeconds > 0.0f ? 1.0f / deltaSeconds : 0.0f;
    const glm::vec2 viewport = _layer->getViewportSize();
    _statsText->setText(std::format(
        "Frame {}\nDelta {:.2f} ms\nFPS {:.1f}\nViewport {:.0f} x {:.0f}",
        app ? app->getHostTick() : 0,
        deltaSeconds * 1000.0f,
        fps,
        viewport.x,
        viewport.y));
}

} // namespace ya
