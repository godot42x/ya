#include "GameEditor/UI/Tabs/EditorUIDesignerTab.h"

#include "Core/Log.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/Panels/UIDesignerPanel.h"
#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"

namespace ya
{

EditorUIDesignerTab::EditorUIDesignerTab(FEditorTabSpawnContext& ctx)
    : EditorNestedDockHost("UIDesignerBody", kUIEditorRootId, ctx)
    , _layer(ctx.layer)
{
    enableTick();
    applyNestedFactoryLayout(EditorDockWorkspace::factoryOwnedNestedLayoutFor(kUIEditorRootId));
}

void EditorUIDesignerTab::construct()
{
    if (!_nestedDock) {
        return;
    }
    auto status = ui::text("UIDesignerHostStatus").setText("No document open").setStyleKey("text.muted").share();
    auto newBuilder = ui::button("UIDesignerNew").child(ui::text("UIDesignerNewLabel").setText("New Panel"));
    newBuilder.setOnClick([this]() {
        if (_layer) {
            _layer->getUIDesignerPanel().newDocument("panel");
        }
    });
    auto saveBuilder = ui::button("UIDesignerSave").child(ui::text("UIDesignerSaveLabel").setText("Save"));
    saveBuilder.setOnClick([this]() {
        if (_layer) {
            (void)_layer->getUIDesignerPanel().saveDocument();
        }
    });
    auto closeBuilder = ui::button("UIDesignerClose").child(ui::text("UIDesignerCloseLabel").setText("Close"));
    closeBuilder.setOnClick([this]() {
        if (_layer && !_layer->getUIDesignerPanel().closeDocument()) {
            YA_CORE_WARN("UI Designer: close rejected (document is dirty; save first)");
        }
    });
    _statusText = status;
    _newButton  = newBuilder.share();
    _saveButton = saveBuilder.share();
    _closeButton = closeBuilder.share();

    addDetachedChild(ui::column("UIDesignerHostColumn")
                         .setSpacing(6.0f)
                         .child(ui::row("UIDesignerToolbar")
                                    .setSpacing(6.0f)
                                    .child(_statusText, ui::boxSlot().fill())
                                    .child(_newButton, ui::boxSlot().preferredSize({120.0f, 24.0f}))
                                    .child(_saveButton, ui::boxSlot().preferredSize({80.0f, 24.0f}))
                                    .child(_closeButton, ui::boxSlot().preferredSize({80.0f, 24.0f})),
                                ui::boxSlot().preferredSize({0.0f, 28.0f}))
                         .child(ui::dockSpace("UIDesignerDock").setContext(_nestedDock).release(),
                                ui::boxSlot().fill())
                         .release());
}

void EditorUIDesignerTab::onAttached()
{
    refreshStatus();
}

void EditorUIDesignerTab::tick(float)
{
    refreshStatus();
}

void EditorUIDesignerTab::refreshStatus()
{
    if (!_layer || !_statusText) {
        return;
    }
    UIDesignerPanel& designer = _layer->getUIDesignerPanel();
    const auto& document = designer.getOpenDocument();
    std::string status = document ? "UI: " + document->typeId : "No UI document";
    if (designer.isDocumentDirty()) {
        status += " *";
    }
    _statusText->setText(status);
    const bool bOpen = document != nullptr;
    if (_saveButton) {
        _saveButton->setEnabled(bOpen);
    }
    if (_closeButton) {
        _closeButton->setEnabled(bOpen);
    }
}

} // namespace ya
