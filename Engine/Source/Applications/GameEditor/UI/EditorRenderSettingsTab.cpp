#include "GameEditor/UI/EditorRenderSettingsTab.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GameEditor/UI/RuntimeRenderSettingsSection.h"

namespace ya
{

EditorRenderSettingsTab::EditorRenderSettingsTab(App* app, IRenderSurfaceContext* presentSurface)
    : UICompoundWidget("RenderSettingsBody", "panel.canvas")
    , _app(app)
    , _presentSurface(presentSurface)
{
    enableTick();
}

void EditorRenderSettingsTab::construct()
{
    auto settings = std::make_shared<RuntimeRenderSettingsSection>("RuntimeRenderSettings", _app, _presentSurface);
    _settings = settings;

    addDetachedChild(ui::scroll("RenderSettingsScroll")
                         .setAxis(EScrollAxis::Vertical)
                         .child(ui::column("RenderSettingsColumn")
                                    .setSpacing(8.0f)
                                    .setPadding({12.0f, 12.0f})
                                    .setStretchLastChild(false)
                                    .child(settings)
                                    .release(),
                                ui::contentSlot()
                                    .hAlign(EUIOverlayAlignment::Fill)
                                    .vAlign(EUIOverlayAlignment::Start))
                         .release());
}

void EditorRenderSettingsTab::onAttached()
{
    refresh();
}

void EditorRenderSettingsTab::tick(float)
{
    refresh();
}

void EditorRenderSettingsTab::refresh()
{
    if (!_settings) {
        return;
    }
    _settings->sync(_app, _presentSurface);
}

} // namespace ya
