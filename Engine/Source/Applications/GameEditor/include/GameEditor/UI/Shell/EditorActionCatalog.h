#pragma once

#include <functional>

namespace ya
{

class ActionMap;
class UndoStack;
struct EditorLayer;

/// Rebuild-period editor ActionMap catalog. Surface calls this once after
/// constructing a fresh ActionMap; menus and toolbar then execute by id.
void registerEditorActions(ActionMap& actions,
                           EditorLayer& layer,
                           UndoStack& undo,
                           std::function<void()> openSceneSaveAs,
                           std::function<void()> openEditorSettings);

} // namespace ya
