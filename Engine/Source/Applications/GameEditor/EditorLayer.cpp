#include "GameEditor/EditorLayerInternal.h"
#include "GameEditor/Panels/UIDesignerPanel.h"
#include "ECS/Component.h"
#include "ECS/Systems/Components/CameraComponent.h"

namespace ya
{
EditorLayer::EditorLayer(App* app)
    : _app(app),
      _sceneHierarchyPanel(this),
      _assetInspectorPanel(this),
      _uiDesignerPanel(this)
{
    _gizmo.bind(_app, FEditorViewportGizmoSources{
        .getSelectedEntity = [this]() { return getSelectedEntity(); },
        .getSelections = [this]() -> const std::vector<Entity*>& { return getSelections(); },
        .getViewportInteractionScene = [this]() { return getViewportInteractionScene(); },
        .isViewportMode2D = [this]() { return isViewportMode2D(); },
    });
}

bool EditorLayer::shouldCaptureInput() const
{
    if (bViewportFocused || bViewportHovered) {
        return true;
    }
    return _app && _app->getInputManager().isMouseButtonPressed(EMouse::Right);
}

void EditorLayer::onAttach()
{
    YA_PROFILE_FUNCTION();
    YA_CORE_INFO("EditorLayer::onAttach");

    if (!_app)
        return;

    syncEditorSettingsFromConfig();

    // Initialize editor panels
    if (auto scene = getEditableScene())
    {
        _sceneHierarchyPanel.setContext(scene);
        notifyHierarchyChanged();
    }

    if (!hasProjectLoaded()) {
        refreshProjectBrowser();
    }
}

void EditorLayer::onDetach()
{
    YA_CORE_INFO("EditorLayer::onDetach");
    _uiDesignerPanel.abandonDocument();
    // Unsubscribe from scene manager events
    if (_app) {
        if (auto* sceneManager = _app->getSceneServices().getSceneManager()) {
            sceneManager->onSceneActivated.removeAll(this);
        }
    }
}

void EditorLayer::onUpdate(float dt)
{
    YA_PROFILE_FUNCTION();
    _lastDeltaTime = dt;
}

Entity* EditorLayer::getCameraPreviewEntity() const
{
    if (isViewportMode2D()) {
        return nullptr;
    }
    Entity* selected = getSelectedEntity();
    if (!selected || !selected->isValid() || !selected->hasComponent<CameraComponent>() ||
        !selected->hasComponent<TransformComponent>()) {
        return nullptr;
    }
    return selected;
}

void EditorLayer::setEditableScene(Scene* scene)
{
    _editableScene = scene;
    _sceneHierarchyPanel.setContext(getSceneHierarchyContext());
}

void EditorLayer::setDocumentRegistry(EditorDocumentRegistry* documents)
{
    _documents = documents;
    _uiDesignerPanel.bindDocuments(documents);
}

void EditorLayer::setCurrentScenePath(std::string scenePath)
{
    if (_currentScenePath == scenePath) {
        return;
    }
    _currentScenePath = std::move(scenePath);
    onScenePathChanged.broadcast();
}

void EditorLayer::setViewportMode(EViewportMode mode, bool bPersist)
{
    if (_viewportMode == mode) {
        return;
    }

    _viewportMode = mode;
    _gizmo.cancelDrag();
    _sceneHierarchyPanel.setContext(getSceneHierarchyContext());

    // Cancel any in-flight 2D canvas manipulation on mode switch.
    _canvasPressHit     = nullptr;
    _canvasPressPoint   = {0.0f, 0.0f};
    _bCanvasPressActive = false;
    _uiDesignerPanel.endDrag();

    if (_app && mode == EViewportMode::Mode2D) {
        _app->getInputRouter().cancelInput(EInputCancelReason::CaptureReleased);
    }

    if (bPersist) {
        ConfigManager::Editor(kEditorConfigDocument)
            .set("viewport.mode", mode == EViewportMode::Mode2D ? "2d" : "3d")
            .flush();
    }
}

bool EditorLayer::viewportToCanvas(const glm::vec2& viewportLocal, glm::vec2& outCanvas) const
{
    if (_canvasZoom <= 0.0f) {
        return false;
    }
    outCanvas = (viewportLocal - _canvasPan) / _canvasZoom;
    return outCanvas.x >= 0.0f && outCanvas.y >= 0.0f &&
           outCanvas.x <= _viewportSize.x && outCanvas.y <= _viewportSize.y;
}

glm::vec2 EditorLayer::canvasToViewport(const glm::vec2& canvasPoint) const
{
    return canvasPoint * _canvasZoom + _canvasPan;
}


void EditorLayer::notifyViewportWidgetRect(const Rect2D& rect)
{
    _viewportBounds[0] = rect.pos;
    _viewportBounds[1] = rect.pos + rect.extent;
    viewportRect       = rect;
    _viewportMouseRect = rect;
    _viewportMouseCenter = {
        rect.pos.x + rect.extent.x * 0.5f,
        rect.pos.y + rect.extent.y * 0.5f,
    };

    // Panel geometry only. The editor declares its authoring View with this
    // rect, so a change here is a declaration fact, not something to push into
    // host state and read back.
    if (rect.extent.x > 0.0f && rect.extent.y > 0.0f) {
        _viewportSize = rect.extent;
    }
}

void EditorLayer::setViewportHoverFocus(bool hovered, bool focused)
{
    bViewportHovered = hovered;
    bViewportFocused = focused;
}

void EditorLayer::cmdNewScene()
{
    App::get()->getTaskManager().registerTickTask([this]() {
        auto* app = App::get();
        if (!app) {
            return;
        }

        auto* sceneManager = app->getSceneServices().getSceneManager();
        if (sceneManager && sceneManager->hasScene()) {
            if (auto* render = app->getRenderServices().getRender()) {
                render->waitIdle();
            }
        }
        auto scene = makeShared<Scene>();
        if (sceneManager) {
            sceneManager->unloadScene();
            sceneManager->activateScene(scene);
        }
        setCurrentScenePath({});
    });
}

void EditorLayer::cmdLoadScene(std::string scenePath)
{
    if (!_app || scenePath.empty()) {
        return;
    }
    _app->getTaskManager().registerTickTask([this, scenePath = std::move(scenePath)]() {
        if (!_app) {
            return;
        }
        if (_app->getSceneServices().loadScene(scenePath)) {
            setCurrentScenePath(scenePath);
        }
    });
}

void EditorLayer::cmdSaveScene()
{
    if (!_currentScenePath.empty()) {
        if (_app && _app->getSceneServices().getSceneManager()) {
            if (auto* scene = getEditableScene()) {
                (void)scene;
                _app->getSceneServices().saveScene(_currentScenePath);
                YA_CORE_INFO("Scene saved to: {}", _currentScenePath);
            }
        }
        return;
    }
    cmdSaveSceneAs();
}

void EditorLayer::cmdSaveSceneAs()
{
    if (_saveSceneAsHandler) {
        _saveSceneAsHandler();
        return;
    }

    std::string defaultName = "NewScene";
    if (_app && _app->getSceneServices().getSceneManager()) {
        if (auto* scene = getEditableScene(); scene && !scene->getName().empty()) {
            defaultName = scene->getName();
        }
    }

    _filePicker.openSceneSavePicker(
        defaultName,
        [this](const std::string& selectedDir, const std::string& sceneName) {
            _currentScenePath = selectedDir + "/" + sceneName + ".scene.json";
            if (_app && _app->getSceneServices().getSceneManager()) {
                if (auto* scene = getEditableScene()) {
                    scene->setName(sceneName);
                    _app->getSceneServices().saveScene(_currentScenePath);
                    YA_CORE_INFO("Scene saved to: {}", _currentScenePath);
                }
            }
        });
}

} // namespace ya
