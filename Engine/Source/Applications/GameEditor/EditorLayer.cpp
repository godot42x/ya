#include "GameEditor/EditorLayerInternal.h"

namespace ya
{
EditorLayer::EditorLayer(App* app)
    : _app(app),
      _sceneHierarchyPanel(this),
      _assetInspectorPanel(this),
      _uiDesignerPanel(this)
{
}

void EditorLayer::onAttach()
{
    YA_PROFILE_FUNCTION();
    YA_CORE_INFO("EditorLayer::onAttach");

    if (!_app)
        return;

    syncEditorSettingsFromConfig();
    loadImGuiSettingsFromConfig();
    saveImGuiSettingsToConfig();

    // Initialize editor panels
    if (auto scene = getEditableScene())
    {
        _sceneHierarchyPanel.setContext(scene);
        notifyHierarchyChanged();
    }

    auto am             = AssetManager::get();
    auto fileTexture    = am->loadTextureSync("file", "Engine/Content/TestTextures/editor/file.png").get();
    auto folderTexture  = am->loadTextureSync("folder", "Engine/Content/TestTextures/editor/folder2.png").get();
    auto sampler        = TextureLibrary::get().getDefaultSampler();
    _filePicker.setIcons(getOrCreateImGuiTextureID(fileTexture->getImageView(), sampler),
                         getOrCreateImGuiTextureID(folderTexture->getImageView(), sampler));
    _filePicker.setDefaultViewMode(FileExplorer::ViewMode::Icon);
    auto playIcon       = am->loadTextureSync("play", "Engine/Content/TestTextures/editor/play.png");
    auto pauseIcon      = am->loadTextureSync("pause", "Engine/Content/TestTextures/editor/pause.png");
    auto stopIcon       = am->loadTextureSync("stop", "Engine/Content/TestTextures/editor/stop.png");
    auto simulationIcon = am->loadTextureSync("simulate_button", "Engine/Content/TestTextures/editor/simulate_button.png");

    // Validate texture loading
    if (!playIcon) YA_CORE_ERROR("Failed to load play icon");
    if (!pauseIcon) YA_CORE_ERROR("Failed to load pause icon");
    if (!stopIcon) YA_CORE_ERROR("Failed to load stop icon");
    if (!simulationIcon) YA_CORE_ERROR("Failed to load simulation icon");

    _playIcon       = getOrCreateImGuiTextureID(playIcon->getImageView());
    _pauseIcon      = getOrCreateImGuiTextureID(pauseIcon->getImageView());
    _stopIcon       = getOrCreateImGuiTextureID(stopIcon->getImageView());
    _simulationIcon = getOrCreateImGuiTextureID(simulationIcon->getImageView());

    if (!hasProjectLoaded()) {
        refreshProjectBrowser();
    }
}

void EditorLayer::onDetach()
{
    YA_CORE_INFO("EditorLayer::onDetach");
    // Unsubscribe from scene manager events
    if (_app) {
        if (auto* sceneManager = _app->getSceneServices().getSceneManager()) {
            sceneManager->onSceneActivated.removeAll(this);
        }
    }
    // Cleanup ImGui textures before destroying panels
    cleanupImGuiTextures();
}

void EditorLayer::onUpdate(float dt)
{
    YA_PROFILE_FUNCTION();
    _lastDeltaTime = dt;
}

void EditorLayer::setEditableScene(Scene* scene)
{
    _editableScene = scene;
    _sceneHierarchyPanel.setContext(getSceneHierarchyContext());
}

void EditorLayer::setViewportMode(EViewportMode mode, bool bPersist)
{
    if (_viewportMode == mode) {
        return;
    }

    _viewportMode = mode;
    cancelViewportGizmoDrag();
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

    const bool bMouseCaptured = _app && _app->getInputRouter().isMouseCaptured();
    if (!bMouseCaptured &&
        (_viewportSize.x != rect.extent.x || _viewportSize.y != rect.extent.y) &&
        rect.extent.x > 0.0f && rect.extent.y > 0.0f) {
        _viewportSize = rect.extent;
        queueViewportResize(rect);
    }
}

void EditorLayer::setViewportHoverFocus(bool hovered, bool focused)
{
    bViewportHovered = hovered;
    bViewportFocused = focused;
}

void EditorLayer::cmdNewScene()
{
    App::get()->getTaskManager().registerFrameTask([this]() {
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
        _currentScenePath.clear();
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
