#pragma once

#include <functional>

namespace ya
{

class ActionMap;
class UndoStack;
struct EditorLayer;
struct EditorUIDesignerSession;

/// Rebuild-period editor ActionMap catalog. Surface calls this once after
/// constructing a fresh ActionMap; menus and toolbar then execute by id.
void registerEditorActions(ActionMap& actions,
                           EditorLayer& layer,
                           UndoStack& undo,
                           std::function<void()> openSceneSaveAs,
                           std::function<void()> openEditorSettings);

/// The UI Designer page's own actions. Ids are page-agnostic verbs
/// (edit.undo, selection.delete, ...): shortcuts and the Edit menu resolve them
/// on the active page's root, so a UI edit never reaches the Level document.
void registerUIDesignerActions(ActionMap& actions, EditorUIDesignerSession& designer);

} // namespace ya
