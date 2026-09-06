#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct EditorLayer;
struct IEditorViewportHostSink;
struct SelectionModel;
struct UIElement;
class UndoStack;
class ActionMap;
struct WidgetTree;

struct FEditorTabSpawnContext
{
    WidgetTree&      tree;
    EditorLayer&     layer;
    SelectionModel&  selection;
    ActionMap&       actions;
    UndoStack&       undo;
    IEditorViewportHostSink* viewportHost = nullptr;
};

struct FEditorTabSpawner
{
    std::string tabId;
    std::string title;
    std::string toolsMenuLabel;
    std::function<std::shared_ptr<UIElement>(FEditorTabSpawnContext&)> spawn;
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
