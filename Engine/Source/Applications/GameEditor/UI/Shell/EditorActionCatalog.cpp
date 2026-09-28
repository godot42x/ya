#include "GameEditor/UI/Shell/EditorActionCatalog.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "GUI/Binding/ActionMap.h"
#include "GUI/Binding/UndoStack.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/EditorUIDesignerSession.h"
#include "GameEditor/UI/Ops/EditorHierarchyOps.h"
#include "GameRuntime/App.h"

namespace ya
{

void registerEditorActions(ActionMap& actions,
                           EditorLayer& layer,
                           UndoStack& undo,
                           std::function<void()> openSceneSaveAs,
                           std::function<void()> openEditorSettings)
{
    auto define = [&actions](FAction action) {
        if (!actions.define(std::move(action))) {
            YA_CORE_ERROR("EditorSurface: failed to define action");
        }
    };

    define({
        .id      = "scene.new",
        .label   = "New Scene",
        .chord   = FActionChord::primary(EKey::K_N),
        .execute = [&layer]() { layer.cmdNewScene(); },
    });
    define({
        .id      = "scene.open",
        .label   = "Open Scene",
        .chord   = FActionChord::primary(EKey::K_O),
        .execute = [&layer]() { layer.cmdOpenScene(); },
    });
    define({
        .id      = "scene.save",
        .label   = "Save Scene",
        .chord   = FActionChord::primary(EKey::K_S),
        .execute = [&layer]() { layer.cmdSaveScene(); },
    });
    define({
        .id      = "scene.saveAs",
        .label   = "Save Scene As",
        .chord   = FActionChord::primary(EKey::K_S, true),
        .execute = [openSceneSaveAs = std::move(openSceneSaveAs)]() {
            if (openSceneSaveAs) {
                openSceneSaveAs();
            }
        },
    });
    define({
        .id         = "edit.undo",
        .label      = "Undo",
        .chord      = FActionChord::primary(EKey::K_Z),
        .execute    = [&undo]() { (void)undo.undo(); },
        .canExecute = [&undo]() { return undo.canUndo(); },
    });
    const FActionChord redoChord =
#if defined(__APPLE__)
        FActionChord::primary(EKey::K_Z, true);
#else
        FActionChord::primary(EKey::K_Y);
#endif
    define({
        .id         = "edit.redo",
        .label      = "Redo",
        .chord      = redoChord,
        .execute    = [&undo]() { (void)undo.redo(); },
        .canExecute = [&undo]() { return undo.canRedo(); },
    });
    define({
        .id         = "selection.createEmpty",
        .label      = "Create Empty Node",
        .execute    = [&layer]() { layer.cmdCreateEmptyNode(); },
        .canExecute = [&layer]() { return layer.canViewportAuthor(); },
    });
    define({
        .id         = "selection.duplicate",
        .label      = "Duplicate",
        .chord      = FActionChord::primary(EKey::K_D),
        .execute    = [&layer]() { layer.cmdDuplicateSelection(); },
        .canExecute = [&layer]() {
            return layer.canViewportAuthor() &&
                   !layer.getSelections().empty() &&
                   !editorSelectionIsAllInstanceChildren(layer);
        },
    });
    define({
        .id         = "selection.delete",
        .label      = "Delete",
        .chord      = {.key = EKey::Delete},
        .execute    = [&layer]() { layer.cmdDeleteSelection(); },
        .canExecute = [&layer]() {
            return layer.canViewportAuthor() &&
                   !layer.getSelections().empty() &&
                   !editorSelectionIsAllInstanceChildren(layer);
        },
    });
    define({
        .id      = "app.exit",
        .label   = "Exit",
        .execute = [&layer]() { layer.cmdRequestQuit(); },
    });
    define({
        .id      = "editor.settings",
        .label   = "Editor Settings...",
        .execute = [openEditorSettings = std::move(openEditorSettings)]() {
            if (openEditorSettings) {
                openEditorSettings();
            }
        },
    });
    define({
        .id      = "viewport.ortho",
        .label   = "Viewport Ortho XY",
        .execute = [&layer]() { layer.setEditorOrthoXY(!layer.isEditorOrthoXY()); },
    });
    define({
        .id      = "runtime.play",
        .label   = "Play",
        .execute = []() {
            if (auto* app = App::get()) {
                app->getTaskManager().registerTickTask([app]() { app->startRuntime(); });
            }
        },
    });
    define({
        .id      = "runtime.simulate",
        .label   = "Simulate",
        .execute = []() {
            if (auto* app = App::get()) {
                app->getTaskManager().registerTickTask([app]() { app->startSimulation(); });
            }
        },
    });
    define({
        .id      = "runtime.stop",
        .label   = "Stop",
        .execute = []() {
            if (auto* app = App::get()) {
                app->getTaskManager().registerTickTask([app]() {
                    if (app->isRuntimeMode()) {
                        app->stopRuntime();
                    }
                    else if (app->isSimulationMode()) {
                        app->stopSimulation();
                    }
                });
            }
        },
    });
}

void registerUIDesignerActions(ActionMap& actions, EditorUIDesignerSession& designer)
{
    auto define = [&actions](FAction action) {
        if (!actions.define(std::move(action))) {
            YA_CORE_ERROR("EditorSurface: failed to define UI Designer action");
        }
    };
    define({
        .id         = "ui.save",
        .label      = "Save UI",
        .chord      = FActionChord::primary(EKey::K_S),
        .execute    = [&designer]() { (void)designer.saveDocument(); },
        .canExecute = [&designer]() { return designer.hasDocument(); },
    });
    define({
        .id         = "edit.undo",
        .label      = "Undo",
        .chord      = FActionChord::primary(EKey::K_Z),
        .execute    = [&designer]() { (void)designer.undoStack().undo(); },
        .canExecute = [&designer]() { return designer.undoStack().canUndo(); },
    });
    define({
        .id    = "edit.redo",
        .label = "Redo",
#if defined(__APPLE__)
        .chord = FActionChord::primary(EKey::K_Z, true),
#else
        .chord = FActionChord::primary(EKey::K_Y),
#endif
        .execute    = [&designer]() { (void)designer.undoStack().redo(); },
        .canExecute = [&designer]() { return designer.undoStack().canRedo(); },
    });
    define({
        .id         = "selection.duplicate",
        .label      = "Duplicate Widget",
        .chord      = FActionChord::primary(EKey::K_D),
        .execute    = [&designer]() { (void)designer.duplicateWidget(designer.getSelectedWidget()); },
        .canExecute = [&designer]() {
            UIElement* selected = designer.getSelectedWidget();
            return selected && selected != designer.getPreviewRoot();
        },
    });
    define({
        .id         = "selection.delete",
        .label      = "Delete Widget",
        .chord      = {.key = EKey::Delete},
        .execute    = [&designer]() { (void)designer.deleteWidget(designer.getSelectedWidget()); },
        .canExecute = [&designer]() {
            UIElement* selected = designer.getSelectedWidget();
            return selected && selected != designer.getPreviewRoot();
        },
    });
}

} // namespace ya
