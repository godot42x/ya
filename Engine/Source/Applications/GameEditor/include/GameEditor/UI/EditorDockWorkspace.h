#pragma once

#include "GameEditor/UI/EditorTabSpawnerRegistry.h"

#include <string_view>

namespace ya
{

struct WidgetTree;
struct EditorLayer;
struct SelectionModel;
class ActionMap;
class UndoStack;
struct UIElement;
struct UIMenuBar;
struct FDockContext;
struct IEditorViewportHostSink;

/// Rebuild-period dock / workspace policy. Surface creates the DockSpace
/// chrome, then this object materializes tabs, default layout, persist, and
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
        UIElement*                 authoringParent = nullptr;
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

    [[nodiscard]] FEditorTabSpawnContext makeSpawnContext() const;
    void buildToolsMenu();
    void materializeWorkspaceTabs();
    bool materializeTab(std::string_view tabId);
    bool invokeTab(std::string_view tabId);
    void applyDefaultLayout();
    bool tryRestoreLayout();
    void persistLayout();
};

} // namespace ya
