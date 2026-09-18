#pragma once

#include "Core/Common/Types.h"
#include "GUI/Binding/ActionMap.h"
#include "GUI/Binding/SelectionModel.h"
#include "GUI/Binding/UndoStack.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include <memory>

namespace ya
{

using EditorRootId = uint32_t;
inline constexpr EditorRootId kInvalidEditorRootId  = 0;
inline constexpr EditorRootId kLevelEditorRootId    = 1;
inline constexpr EditorRootId kUIEditorRootId       = 2;
inline constexpr EditorRootId kMaterialEditorRootId = 3;
inline constexpr EditorRootId kScriptEditorRootId   = 4;

[[nodiscard]] inline const char* editorRootTabId(EditorRootId id)
{
    switch (id) {
    case kLevelEditorRootId: {
        return "level-editor";
    }
    case kUIEditorRootId: {
        return "ui-designer";
    }
    case kMaterialEditorRootId: {
        return "material-editor";
    }
    case kScriptEditorRootId: {
        return "script-editor";
    }
    default: {
        return nullptr;
    }
    }
}

[[nodiscard]] inline EEditorDocumentKind editorDocumentKindForRoot(EditorRootId id)
{
    switch (id) {
    case kUIEditorRootId: {
        return EEditorDocumentKind::UI;
    }
    case kMaterialEditorRootId: {
        return EEditorDocumentKind::Material;
    }
    case kScriptEditorRootId: {
        return EEditorDocumentKind::Script;
    }
    default: {
        return EEditorDocumentKind::Scene;
    }
    }
}

using EditorWindowId = uint32_t;
inline constexpr EditorWindowId kInvalidEditorWindowId = 0;
inline constexpr EditorWindowId kDefaultEditorWindowId = 1;

enum class EEditorTabScope : uint8_t
{
    WindowRootEditor,
    EditorOwnedTool,
    WindowTool,
};

/// Where a tab is allowed to live. Owned tools only dock in their owner's
/// nested dock. WindowRootEditor pages occupy the chrome page-tab well
/// (and the matching hideTabBar page leaf). WindowTool only docks as an
/// inner window-root leaf.
enum class EEditorTabPlacement : uint8_t
{
    WindowRootDock,
    EditorOwnedNested,
    WindowPageTab,
};

enum class EEditorTabDetachPolicy : uint8_t
{
    Locked,
    TearOffKeepOwner,
    IndependentWindow,
};

struct FEditorTabOwnership
{
    EEditorTabScope scope          = EEditorTabScope::WindowTool;
    EditorRootId    ownerEditorId = kInvalidEditorRootId;
};

/// Spawn / Window-menu placement. Owned tools materialize in their owner's
/// nested dock; WindowTool / WindowRootEditor materialize on the window-root.
[[nodiscard]] inline bool canSpawnEditorTab(const FEditorTabOwnership& tab,
                                            EEditorTabPlacement targetPlacement,
                                            EditorRootId targetRootId)
{
    if (targetPlacement == EEditorTabPlacement::EditorOwnedNested) {
        return tab.scope == EEditorTabScope::EditorOwnedTool &&
               tab.ownerEditorId != kInvalidEditorRootId &&
               tab.ownerEditorId == targetRootId;
    }
    if (targetPlacement == EEditorTabPlacement::WindowPageTab) {
        return tab.scope == EEditorTabScope::WindowRootEditor;
    }
    if (targetPlacement != EEditorTabPlacement::WindowRootDock) {
        return false;
    }
    return tab.scope == EEditorTabScope::WindowRootEditor || tab.scope == EEditorTabScope::WindowTool;
}

/// Drop / redock. Level Editor lets Viewport/Hierarchy/Inspector and window
/// tools share both the window-root dock and the Level nested dock. Spawn
/// still follows `canSpawnEditorTab` so Window-menu does not duplicate tabs.
[[nodiscard]] inline bool canDockEditorTab(const FEditorTabOwnership& tab,
                                           EEditorTabPlacement targetPlacement,
                                           EditorRootId targetRootId)
{
    if (canSpawnEditorTab(tab, targetPlacement, targetRootId)) {
        return true;
    }
    if (targetPlacement == EEditorTabPlacement::EditorOwnedNested &&
        targetRootId == kLevelEditorRootId &&
        tab.scope == EEditorTabScope::WindowTool) {
        return true;
    }
    if (targetPlacement == EEditorTabPlacement::WindowRootDock &&
        tab.scope == EEditorTabScope::EditorOwnedTool &&
        tab.ownerEditorId == kLevelEditorRootId &&
        (targetRootId == kLevelEditorRootId || targetRootId == kInvalidEditorRootId)) {
        return true;
    }
    return false;
}

[[nodiscard]] inline bool isLevelEditorSharedDockTab(const FEditorTabOwnership& tab)
{
    if (tab.scope == EEditorTabScope::WindowTool) {
        return true;
    }
    return tab.scope == EEditorTabScope::EditorOwnedTool && tab.ownerEditorId == kLevelEditorRootId;
}

/// Typed GameEditor drag payload. GUI `FDockPanelDragDropOp` stays generic;
/// Editor maps panel identity onto this before policy / native tear-off.
struct FEditorTabDragPayload
{
    std::string            tabId;
    EEditorTabScope        scope           = EEditorTabScope::WindowTool;
    EditorRootId           ownerEditorId   = kInvalidEditorRootId;
    std::string            documentKey;
    EEditorTabDetachPolicy detachPolicy    = EEditorTabDetachPolicy::IndependentWindow;
    EditorWindowId         sourceWindowId  = kInvalidEditorWindowId;
    EEditorTabPlacement    sourcePlacement = EEditorTabPlacement::WindowRootDock;
};

[[nodiscard]] inline bool canTearOffEditorTab(EEditorTabDetachPolicy policy)
{
    return policy != EEditorTabDetachPolicy::Locked;
}

[[nodiscard]] inline bool canTearOffEditorTab(const FEditorTabDragPayload& payload)
{
    return canTearOffEditorTab(payload.detachPolicy);
}

[[nodiscard]] inline bool canAcceptEditorDrop(const FEditorTabDragPayload& payload,
                                              EEditorTabPlacement targetPlacement,
                                              EditorRootId targetRootId)
{
    return canDockEditorTab({.scope = payload.scope, .ownerEditorId = payload.ownerEditorId},
                            targetPlacement,
                            targetRootId);
}

[[nodiscard]] inline EEditorTabPlacement homePlacementFor(const FEditorTabDragPayload& payload)
{
    return payload.scope == EEditorTabScope::EditorOwnedTool ? EEditorTabPlacement::EditorOwnedNested
                                                             : EEditorTabPlacement::WindowRootDock;
}

[[nodiscard]] inline EditorRootId homeRootFor(const FEditorTabDragPayload& payload)
{
    if (payload.scope == EEditorTabScope::EditorOwnedTool) {
        return payload.ownerEditorId;
    }
    return payload.ownerEditorId != kInvalidEditorRootId ? payload.ownerEditorId : kLevelEditorRootId;
}

/// Locked tabs cannot leave their materialized dock. Extra windows may close;
/// the default Level window cannot.
[[nodiscard]] inline bool canRedockEditorTab(const FEditorTabDragPayload& payload)
{
    return canTearOffEditorTab(payload) &&
           canAcceptEditorDrop(payload, homePlacementFor(payload), homeRootFor(payload));
}

[[nodiscard]] inline bool canCloseEditorWindow(EditorWindowId windowId)
{
    return windowId != kInvalidEditorWindowId && windowId != kDefaultEditorWindowId;
}

/// One WindowRootEditor's selection / actions. Undo is the bound document's
/// stack when a document session is attached; otherwise a local fallback.
/// EditorSurface does not own these models.
struct EditorRootSession
{
private:
    EditorRootId                    _id        = kLevelEditorRootId;
    std::shared_ptr<SelectionModel> _selection = std::make_shared<SelectionModel>();
    std::shared_ptr<ActionMap>      _actions   = std::make_shared<ActionMap>();
    std::shared_ptr<UndoStack>      _undo      = std::make_shared<UndoStack>();
    EditorDocumentSession*          _document  = nullptr;

public:
    explicit EditorRootSession(EditorRootId id = kLevelEditorRootId)
        : _id(id)
    {
    }

    [[nodiscard]] EditorRootId id() const { return _id; }
    [[nodiscard]] SelectionModel& selection() { return *_selection; }
    [[nodiscard]] const SelectionModel& selection() const { return *_selection; }
    [[nodiscard]] ActionMap& actions() { return *_actions; }
    [[nodiscard]] const ActionMap& actions() const { return *_actions; }

    void bindDocument(EditorDocumentSession* document)
    {
        if (_document == document) {
            return;
        }
        if (_document) {
            _document->releaseBind();
        }
        _document = document;
        if (_document) {
            _document->addBind();
        }
    }

    [[nodiscard]] EditorDocumentSession* document() { return _document; }
    [[nodiscard]] const EditorDocumentSession* document() const { return _document; }

    [[nodiscard]] UndoStack& undo() { return _document ? _document->undo() : *_undo; }
    [[nodiscard]] const UndoStack& undo() const { return _document ? _document->undo() : *_undo; }
};

/// Window-owned root editors. Surface stores pointers; WindowSession owns
/// the sessions. Not a window manager.
struct FEditorRootSessions
{
    EditorRootSession* level    = nullptr;
    EditorRootSession* ui       = nullptr;
    EditorRootSession* material = nullptr;
    EditorRootSession* script   = nullptr;

    [[nodiscard]] EditorRootSession* find(EditorRootId id) const
    {
        switch (id) {
        case kUIEditorRootId: {
            return ui;
        }
        case kMaterialEditorRootId: {
            return material;
        }
        case kScriptEditorRootId: {
            return script;
        }
        default: {
            return level;
        }
        }
    }
};

} // namespace ya
