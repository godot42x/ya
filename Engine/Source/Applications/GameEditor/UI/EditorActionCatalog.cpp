#include "GameEditor/UI/EditorActionCatalog.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "GUI/Binding/ActionMap.h"
#include "GUI/Binding/UndoStack.h"
#include "GameEditor/EditorLayer.h"
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
        .canExecute = [&layer]() { return layer.canViewportAuthor() && !layer.getSelections().empty(); },
    });
    define({
        .id         = "selection.delete",
        .label      = "Delete",
        .chord      = {.key = EKey::Delete},
        .execute    = [&layer]() { layer.cmdDeleteSelection(); },
        .canExecute = [&layer]() { return layer.canViewportAuthor() && !layer.getSelections().empty(); },
    });
    define({
        .id      = "app.exit",
        .label   = "Exit",
        .execute = []() {
            if (auto* app = App::get()) {
                app->requestQuit();
            }
        },
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
        .id      = "viewport.mode3d",
        .label   = "Viewport 3D",
        .execute = [&layer]() { layer.setViewportMode(EViewportMode::Mode3D); },
    });
    define({
        .id      = "viewport.mode2d",
        .label   = "Viewport 2D",
        .execute = [&layer]() { layer.setViewportMode(EViewportMode::Mode2D); },
    });
    define({
        .id      = "runtime.play",
        .label   = "Play",
        .execute = []() {
            if (auto* app = App::get()) {
                app->getTaskManager().registerFrameTask([app]() { app->startRuntime(); });
            }
        },
    });
    define({
        .id      = "runtime.simulate",
        .label   = "Simulate",
        .execute = []() {
            if (auto* app = App::get()) {
                app->getTaskManager().registerFrameTask([app]() { app->startSimulation(); });
            }
        },
    });
    define({
        .id      = "runtime.stop",
        .label   = "Stop",
        .execute = []() {
            if (auto* app = App::get()) {
                app->getTaskManager().registerFrameTask([app]() {
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

} // namespace ya
