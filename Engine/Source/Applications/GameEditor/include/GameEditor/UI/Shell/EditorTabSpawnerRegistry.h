#pragma once

#include "GameEditor/UI/Shell/EditorRootSession.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct App;
struct EditorLayer;
struct EditorDocumentRegistry;
struct IEditorViewportHostSink;
struct IRenderSurfaceContext;
struct SelectionModel;
struct UIElement;
struct FDockContext;
class UndoStack;
class ActionMap;
struct WidgetTree;
class EditorTabSpawnerRegistry;

/// Inputs a tab factory may consume. selection / actions / undo / viewportHost
/// come from the owner editor session. `app` and `presentSurface` are the
/// window session's process/present handles and may be null. spawn() only
/// constructs UI content; it does not hold EditorSurface, own the tree, or tick.
struct FEditorTabSpawnContext
{
    EditorWindowId   windowId = kDefaultEditorWindowId;
    EEditorTabScope  scope    = EEditorTabScope::WindowRootEditor;
    std::optional<EditorRootId> ownerEditorId;
    std::string      documentKey;
    EEditorTabPlacement    placement    = EEditorTabPlacement::WindowRootDock;
    EEditorTabDetachPolicy detachPolicy = EEditorTabDetachPolicy::IndependentWindow;

    WidgetTree*              tree         = nullptr;
    EditorLayer*             layer        = nullptr;
    SelectionModel*          selection    = nullptr;
    ActionMap*               actions      = nullptr;
    UndoStack*               undo         = nullptr;
    IEditorViewportHostSink* viewportHost = nullptr;
    EditorTabSpawnerRegistry* spawners      = nullptr;
    EditorDocumentRegistry*   documents     = nullptr;
    std::shared_ptr<FDockContext> nestedDock;
    App*                       app            = nullptr;
    IRenderSurfaceContext*     presentSurface = nullptr;
    std::function<EditorRootSession*(EditorRootId)> rootFor;
    EditorRootSession*         ownerRoot      = nullptr;
    /// Pixel floor for the split edge that contains the spawned tab. Does not
    /// rewrite ratio. No-op when the tab is not under a split. Valid only from
    /// onSpawnComplete.
    std::function<void(float)> setMinExtent;
};

struct FEditorTabSpawner
{
    std::string            tabId;
    std::string            title;
    std::string            toolsMenuLabel;
    EEditorTabScope        scope          = EEditorTabScope::WindowRootEditor;
    EditorRootId           ownerEditorId  = kInvalidEditorRootId;
    std::string            documentKey;
    EEditorTabPlacement    placement      = EEditorTabPlacement::WindowRootDock;
    EEditorTabDetachPolicy detachPolicy   = EEditorTabDetachPolicy::IndependentWindow;
    std::function<std::shared_ptr<UIElement>(FEditorTabSpawnContext&)> spawn;
    /// Optional spawn-completion hook for tab-specific initialization. The hook runs
    /// after the factory returns and after the widget is registered in Dock.
    std::function<void(FEditorTabSpawnContext&, UIElement&)> onSpawnComplete;

    [[nodiscard]] FEditorTabOwnership ownership() const
    {
        return {.scope = scope, .ownerEditorId = ownerEditorId};
    }
};

/// Register-only table of single-instance editor tab spawners.
/// Invoke/activate lives on EditorSurface; this object does not own widgets
/// or tree lifecycle.
class EditorTabSpawnerRegistry
{
  public:
    void add(FEditorTabSpawner spawner);
    [[nodiscard]] const FEditorTabSpawner* find(std::string_view tabId) const;
    [[nodiscard]] const std::vector<FEditorTabSpawner>& all() const { return _spawners; }

  private:
    std::vector<FEditorTabSpawner> _spawners;
};

void registerBuiltinEditorTabSpawners(EditorTabSpawnerRegistry& registry);

} // namespace ya
