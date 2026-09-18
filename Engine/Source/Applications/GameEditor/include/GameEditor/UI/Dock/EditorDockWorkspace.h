#pragma once

#include "GameEditor/UI/Shell/EditorRootSession.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace ya
{

struct WidgetTree;
struct App;
struct EditorLayer;
struct EditorDocumentRegistry;
struct SelectionModel;
class ActionMap;
class UndoStack;
struct UIMenuBar;
struct FDockContext;
struct IEditorViewportHostSink;
struct IRenderSurfaceContext;

/// Rebuild-period dock / workspace policy. Surface creates the DockSpace
/// chrome, then this object materializes tabs, layout documents, persist, and
/// Window-menu toggle. Tick does not go through here.
class EditorDockWorkspace
{
  public:
    struct FHost
    {
        WidgetTree*                tree            = nullptr;
        EditorLayer*               layer           = nullptr;
        SelectionModel*            selection       = nullptr;
        ActionMap*                 actions         = nullptr;
        UndoStack*                 undo            = nullptr;
        IEditorViewportHostSink*   viewportHost    = nullptr;
        EditorTabSpawnerRegistry*  spawners        = nullptr;
        EditorDocumentRegistry*    documents       = nullptr;
        FDockContext*              dock            = nullptr;
        UIMenuBar*                 menuBar         = nullptr;
        EditorRootId               activeRootId    = kLevelEditorRootId;
        EEditorTabPlacement        targetPlacement = EEditorTabPlacement::WindowRootDock;
        EditorWindowId             windowId        = kDefaultEditorWindowId;
        std::string                documentKey;
        EditorDockWorkspace*       nestedWorkspace = nullptr;
        std::shared_ptr<FDockContext> nestedDock;
        App*                       app             = nullptr;
        IRenderSurfaceContext*     presentSurface  = nullptr;
        std::function<void()>      persistAll;
        std::function<EditorRootSession*(EditorRootId)> rootFor;
    };

  private:
    FHost _host{};
    void applyAdoptPolicy();
    [[nodiscard]] DockNodeId ensureToolsLeaf();
    /// This dock, or a nested dock that already hosts `tabId`. Spawn must not
    /// run while an instance exists in the same window (WindowTool may live on
    /// the Level nested dock after a user drag / layout restore).
    [[nodiscard]] EditorDockWorkspace* workspaceHoldingTab(std::string_view tabId) const;

  public:
    void bind(FHost host);
    void clear() { _host = {}; }

    /// Window-root factory (level-editor + window tools). Keep in sync with
    /// DefaultEditorDockLayout.json.
    [[nodiscard]] static const nlohmann::json& factoryLayout();
    /// Level-owned nested factory (play-toolbar / viewport / hierarchy / inspector).
    [[nodiscard]] static const nlohmann::json& factoryOwnedNestedLayout();
    /// Nested factory for a document WindowRootEditor (UI / Material / Script).
    [[nodiscard]] static const nlohmann::json& factoryOwnedNestedLayoutFor(EditorRootId rootId);
    /// Map a persisted `editor.dockLayout` document onto this host's placement.
    /// Version 2 envelopes use `windowRoot` / `ownedNested`. Version 1 flat
    /// layouts remap owned tools into the nested factory and inject level-editor.
    [[nodiscard]] static nlohmann::json layoutDocumentForPlacement(const nlohmann::json& document,
                                                                   EEditorTabPlacement placement);

    [[nodiscard]] FEditorTabSpawnContext makeSpawnContext(const FEditorTabSpawner& spawner) const;
    void buildWindowMenu();
    /// Apply user `editor.dockLayout` remapped for this host's placement.
    /// Window-root hosts also apply the nested workspace layout.
    void applyWorkspaceLayout();
    /// Spawn known keys, sanitize unknown keys, import. Falls back to factory
    /// when `bFallbackToFactory` is true and the document cannot be applied.
    bool applyLayoutDocument(const nlohmann::json& layout, bool bFallbackToFactory);
    /// Spawn a tab into this host. `bRestoreLayout` uses drop policy
    /// (`canDockEditorTab`) so a saved layout that the user created by
    /// dragging is not stripped on restart. Invoke / Window-menu keep
    /// `canSpawnEditorTab`.
    bool materializeTab(std::string_view tabId, bool bRestoreLayout = false);
    /// Move window-tool tabs out of the chrome page well when they exist,
    /// and prune abandoned empty Generic / Tools splits. Does not create an
    /// empty Tools well just to host a drop placeholder.
    void repairPlacement();
    /// Activate an existing tab in this dock or a nested dock that already
    /// hosts it. Spawn only when no instance exists and this placement may
    /// spawn the tab (`canSpawnEditorTab`).
    bool invokeTab(std::string_view tabId);
    bool closeTab(std::string_view tabId);
    [[nodiscard]] bool hasTab(std::string_view tabId) const;
    [[nodiscard]] EditorDockWorkspace* nestedWorkspaceFor(EditorRootId rootId) const;
    void resetLayout();
    void persistLayout();
};

} // namespace ya
