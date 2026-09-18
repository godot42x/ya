#include "GameEditor/UI/Tabs/EditorAssetInspectorTab.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"

namespace ya
{

EditorAssetInspectorTab::EditorAssetInspectorTab(EditorLayer& layer)
    : UICompoundWidget("AssetInspectorBody", "panel.canvas")
    , _layer(&layer)
{
    enableTick();
}

void EditorAssetInspectorTab::construct()
{
    auto pathText = ui::text("AssetInspectorPath").setText("No asset selected").setStyleKey("text.muted").share();
    auto statusText = ui::text("AssetInspectorStatus")
                          .setText("Select a texture in Content Browser")
                          .setStyleKey("text.muted")
                          .share();
    auto preview = ui::image("AssetInspectorPreview")
                       .setStyleKey("image")
                       .setScaleMode(EImageScaleMode::Contain)
                       .share();
    _pathText = pathText;
    _statusText = statusText;
    _preview = preview;

    addDetachedChild(ui::border("AssetInspectorBodyInner")
        .setStyleKey("panel.canvas")
        .setPadding(FMargin::all(12.0f))
        .child(ui::column("AssetInspectorColumn")
                   .setSpacing(8.0f)
                   .child(pathText)
                   .child(preview, FBoxSlotArgs{.preferredSize = {0.0f, 220.0f}})
                   .child(statusText)
                   .release(),
               ui::contentSlot().fill())
        .release());
}

void EditorAssetInspectorTab::onAttached()
{
    refresh();
}

void EditorAssetInspectorTab::tick(float)
{
    refresh();
}

void EditorAssetInspectorTab::refresh()
{
    const std::string& path = _layer->getAssetInspectorPanel().inspectedPath();
    _pathText->setText(path.empty() ? "No asset selected" : path);
    _statusText->setText(path.empty() ? "Select a texture in Content Browser" : "Texture preview");
    _preview->setAssetPath(path);
    _preview->setScaleMode(EImageScaleMode::Contain);
    _preview->setResourceMissing(false);
}

} // namespace ya
