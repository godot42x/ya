#include "GameEditor/EditorLayerInternal.h"

namespace ya
{
EditorLayer::EditorLayer(App* app)
    : _app(app),
      _sceneHierarchyPanel(this),
      _detailsView(this),
      _contentBrowserPanel(this),
      _assetInspectorPanel(this),
      _runtimeToolsPanel(this),
      _uiDesignerPanel(this),
      _guiWorkbenchPanel(this)
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
    }

    _contentBrowserPanel.init();

    // Initialize FilePicker with same icons as ContentBrowserPanel
    _filePicker.setIcons(_contentBrowserPanel.folderIcon, _contentBrowserPanel.fileIcon);
    _filePicker.setDefaultViewMode(FileExplorer::ViewMode::Icon);

    auto am             = AssetManager::get();
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

} // namespace ya
