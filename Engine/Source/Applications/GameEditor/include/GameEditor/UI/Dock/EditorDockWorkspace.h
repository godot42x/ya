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
    /// This dock, or a nested dock that already hosts `tabId`. Spawn must not
    /// run while an instance exists in the same window.
    [[nodiscard]] EditorDockWorkspace* workspaceHoldingTab(std::string_view tabId) const;

  public:
    void bind(FHost host);
    void clear() { _host = {}; }

    /// Shipped workspace documents, read through EditorLayoutLibrary (see that
    /// header for the defaults/overrides split). Data rather than literals: the
    /// arrangement is content, and a local override must not need a rebuild.
    ///
    /// Window-root factory (the page well: level-editor + ui-designer).
    [[nodiscard]] static nlohmann::json factoryLayout();
    /// Level-owned nested factory (play-toolbar / viewport / hierarchy / ...).
    [[nodiscard]] static nlohmann::json factoryOwnedNestedLayout();
    /// Nested factory for a document WindowRootEditor (UI / Material / Script).
    [[nodiscard]] static nlohmann::json factoryOwnedNestedLayoutFor(EditorRootId rootId);
    /// The layout document a root's own tool dock is stored under. One name for
    /// both directions: the shipped default and this machine's arrangement are
    /// the same document in two roots, so a page that is closed and reopened
    /// reads back what the user arranged.
    [[nodiscard]] static std::string_view nestedLayoutDocumentName(EditorRootId rootId);
    /// Map a persisted `editor.dockLayout` document onto this host's placement.
    /// v5 envelopes carry `windowRoot` / `ownedNested` v2 dock documents on
    /// their main window record; anything older or malformed falls back to
    /// the factory layout.
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
    /// Spawn a tab into this host when `canSpawnEditorTab` allows it there.
    /// One predicate covers spawn, layout restore, drop and redock, so a
    /// saved layout cannot materialize a panel the live policy would refuse.
    bool materializeTab(std::string_view tabId);
    /// Close non-page panels that a pre-ownership layout left in the chrome
    /// page well, and prune abandoned empty Generic splits. The page well is
    /// pages-only, so there is no window-level well to re-home them into.
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
