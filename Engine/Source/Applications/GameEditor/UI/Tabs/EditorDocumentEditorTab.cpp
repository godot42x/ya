#include "GameEditor/UI/Tabs/EditorDocumentEditorTab.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "GameEditor/UI/Shell/EditorRootSession.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"

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
    _body     = body;
    auto column = ui::column(_name + "Column")
                      .setSpacing(8.0f)
                      .child(ui::text(_name + "Title")
                                 .setText(std::string(kindLabel(_kind)) + " " + roleLabel(_role))
                                 .setStyleKey("text.eyebrow"))
                      .child(body);

    // A script's Preview is its source: readability beats chrome here, so the
    // tab is one scrollable read-only text pane (no editing yet).
    _bHasSource = _kind == EEditorDocumentKind::Script && _role == EEditorDocumentToolRole::Preview;
    if (_bHasSource) {
        auto source = ui::text(_name + "Source").setStyleKey("text.small").setWrap(true).share();
        _sourceText = source;
        auto scroll = ui::scroll(_name + "SourceScroll")
                          .setAxis(EScrollAxis::Vertical)
                          .child(source, ui::contentSlot().fill())
                          .share();
        _sourceScroll = scroll;
        column = std::move(column).child(scroll, ui::boxSlot().fill());
    }
    addDetachedChild(std::move(column).release());
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

    if (_bHasSource) {
        refreshSource(*open);
    }
}

void EditorDocumentToolTab::refreshSource(EditorDocumentSession& open)
{
    if (!_sourceText) {
        return;
    }
    // Re-read only when the file changed under us (a save that made the
    // document clean again, or a different document): the read is not
    // per-frame work.
    const bool bChangedDocument = _sourceKey != open.id().key;
    const bool bSaved           = _sourceDirty && !open.dirty();
    if (!bChangedDocument && !bSaved) {
        return;
    }

    std::string text;
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if (!vfs || !vfs->readFileToString(open.id().key, text)) {
        _sourceText->setText("Cannot read source: " + open.id().key);
    }
    else {
        // Guard the frame budget: a runaway asset must not stall the tab.
        constexpr size_t kMaxPreviewBytes = 64 * 1024;
        if (text.size() > kMaxPreviewBytes) {
            text.resize(kMaxPreviewBytes);
            text += "\n\n... (preview truncated at 64 KiB)";
        }
        _sourceText->setText(text);
    }
    _sourceKey   = open.id().key;
    _sourceDirty = open.dirty();
}

} // namespace ya
