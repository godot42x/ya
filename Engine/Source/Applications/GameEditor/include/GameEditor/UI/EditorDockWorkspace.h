#pragma once

#include "GameEditor/UI/EditorTabSpawnerRegistry.h"

#include <nlohmann/json.hpp>
#include <string_view>

namespace ya
{

struct WidgetTree;
struct EditorLayer;
struct SelectionModel;
class ActionMap;
class UndoStack;
struct UIMenuBar;
struct FDockContext;
struct IEditorViewportHostSink;

/// Rebuild-period dock / workspace policy. Surface creates the DockSpace
/// chrome, then this object materializes tabs, layout documents, persist, and
/// Tools-menu invoke. Tick does not go through here.
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
        FDockContext*              dock            = nullptr;
        UIMenuBar*                 menuBar         = nullptr;
    };

  private:
    FHost _host{};

  public:
    void bind(FHost host) { _host = host; }
    void clear() { _host = {}; }

    /// First-run / reset layout. Keep in sync with DefaultEditorDockLayout.json.
    [[nodiscard]] static const nlohmann::json& factoryLayout();

    [[nodiscard]] FEditorTabSpawnContext makeSpawnContext() const;
    void buildToolsMenu();
    /// Apply user `editor.dockLayout` if present, otherwise the factory document.
    void applyWorkspaceLayout();
    /// Spawn known keys, sanitize unknown keys, import. Falls back to factory
    /// when `bFallbackToFactory` is true and the document cannot be applied.
    bool applyLayoutDocument(const nlohmann::json& layout, bool bFallbackToFactory);
    bool materializeTab(std::string_view tabId);
    bool invokeTab(std::string_view tabId);
    void persistLayout();
};

} // namespace ya
