#include "GameEditor/UI/EditorDocumentEditorTab.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GameEditor/UI/EditorDockWorkspace.h"
#include "GameEditor/UI/EditorRootSession.h"
#include "GameEditor/UI/EditorTabSpawnerRegistry.h"

namespace ya
{

namespace
{

const char* kindLabel(EEditorDocumentKind kind)
{
    switch (kind) {
    case EEditorDocumentKind::Material: {
        return "Material";
    }
    case EEditorDocumentKind::Script: {
        return "Script";
    }
    case EEditorDocumentKind::UI: {
        return "UI";
    }
    default: {
        return "Scene";
    }
    }
}

const char* roleLabel(EEditorDocumentToolRole role)
{
    switch (role) {
    case EEditorDocumentToolRole::Preview: {
        return "Preview";
    }
    case EEditorDocumentToolRole::Parameters: {
        return "Parameters";
    }
    case EEditorDocumentToolRole::Hierarchy: {
        return "Hierarchy";
    }
    default: {
        return "Inspector";
    }
    }
}

} // namespace

EditorDocumentEditorTab::EditorDocumentEditorTab(EditorRootId rootId, FEditorTabSpawnContext& ctx)
    : EditorNestedDockHost(editorRootTabId(rootId) ? editorRootTabId(rootId) : "DocumentEditor",
                           rootId,
                           ctx)
{
    applyNestedFactoryLayout(EditorDockWorkspace::factoryOwnedNestedLayoutFor(rootId));
}

EditorDocumentToolTab::EditorDocumentToolTab(EEditorDocumentKind kind,
                                             EEditorDocumentToolRole role,
                                             FEditorTabSpawnContext& ctx)
    : UICompoundWidget(std::string(kindLabel(kind)) + roleLabel(role) + "Body", "panel.canvas")
    , _kind(kind)
    , _role(role)
    , _ownerRoot(ctx.ownerRoot)
    , _documents(ctx.documents)
    , _documentKey(ctx.documentKey)
{
    enableTick();
}

void EditorDocumentToolTab::construct()
{
    auto body = ui::text(_name + "Text").setStyleKey("text.muted").setText("No document").share();
    _body = body;
    addDetachedChild(ui::column(_name + "Column")
                         .setSpacing(8.0f)
                         .child(ui::text(_name + "Title")
                                    .setText(std::string(kindLabel(_kind)) + " " + roleLabel(_role))
                                    .setStyleKey("text.eyebrow"))
                         .child(body)
                         .release());
}

void EditorDocumentToolTab::onAttached()
{
    refresh();
}

void EditorDocumentToolTab::tick(float)
{
    refresh();
}

EditorDocumentSession* EditorDocumentToolTab::session() const
{
    if (_ownerRoot && _ownerRoot->document() && _ownerRoot->document()->id().kind == _kind) {
        return _ownerRoot->document();
    }
    if (!_documents || _documentKey.empty()) {
        return nullptr;
    }
    return _documents->find({_kind, _documentKey});
}

void EditorDocumentToolTab::refresh()
{
    if (!_body) {
        return;
    }
    EditorDocumentSession* open = session();
    if (!open) {
        _body->setText(_documentKey.empty() ? "No document key. Open from Content Browser."
                                            : "Document '" + _documentKey + "' is not open.");
        return;
    }
    std::string text = "Key: " + open->id().key;
    if (open->dirty()) {
        text += " *";
    }
    text += "; undo=" + std::to_string(open->undo().undoCount());
    if (_role == EEditorDocumentToolRole::Preview) {
        text += open->ownsPreview() ? "; preview owner: yes (PreviewTarget, not Camera)"
                                    : "; preview owner: no";
    }
    if (_role == EEditorDocumentToolRole::Hierarchy) {
        text += "; hierarchy lists this document identity (asset graph arrives later)";
    }
    if (_role == EEditorDocumentToolRole::Inspector) {
        text += "; inspector reflects close policy / dirty, not a Camera";
    }
    _body->setText(text);
}

} // namespace ya
