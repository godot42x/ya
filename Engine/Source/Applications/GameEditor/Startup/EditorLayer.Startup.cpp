#include "GameEditor/EditorLayerInternal.h"
#include "GameEditor/UI/Dialogs/EditorFilePicker.h"

namespace ya
{
Scene* EditorLayer::getEditableScene() const
{
    return _editableScene;
}

Scene* EditorLayer::getSceneHierarchyContext() const
{
    if (isViewportMode2D() && _editableScene) {
        return _editableScene;
    }
    if (_app) {
        if (Scene* activeScene = _app->getSceneServices().getActiveScene()) {
            return activeScene;
        }
    }
    return _editableScene;
}

Scene* EditorLayer::getViewportInteractionScene() const
{
    if (isViewportMode2D()) {
        return _editableScene;
    }
    return _app ? _app->getSceneServices().getActiveScene() : nullptr;
}

SceneWidgetEntry* EditorLayer::getSelectedWidgetEntry()
{
    if (_selectedWidgetEntryId.empty()) {
        return nullptr;
    }
    Scene* scene = getViewportInteractionScene();
    if (!scene) {
        return nullptr;
    }
    for (auto& entry : scene->getWidgetEntries()) {
        if (entry.entryId == _selectedWidgetEntryId) {
            return &entry;
        }
    }
    return nullptr;
}

void EditorLayer::syncEditorSettingsFromConfig()
{
    const std::string defaultScenePath = ConfigManager::get().getOr<std::string>("editor", "startup.defaultScenePath", "");
    strncpy_s(_defaultScenePathBuffer, sizeof(_defaultScenePathBuffer), defaultScenePath.c_str(), _TRUNCATE);
    _bDefaultScenePathDirty = false;
    _bShowViewportCameraOverlay = ConfigManager::get().getOr<bool>("editor",
                                                                   "viewport.cameraOverlay.enabled",
                                                                   _bShowViewportCameraOverlay);
    const std::string viewportMode = ConfigManager::get().getOr<std::string>("editor", "viewport.mode", "3d");
    _viewportMode                  = viewportMode == "2d" ? EViewportMode::Mode2D : EViewportMode::Mode3D;
    _selection.setContext(getSceneHierarchyContext());
}

bool EditorLayer::hasProjectLoaded() const
{
    return _app && _app->getDesc().projectPath.has_value();
}

void EditorLayer::refreshProjectBrowser()
{
    _discoveredProjects.clear();
    _projectBrowserSelection = -1;
    _projectBrowserError.clear();

    namespace fs = std::filesystem;
    // The browser lists the workspace's projects; anchor it at the VFS working
    // root (the engine's own tree) rather than wherever the process was booted.
    const fs::path workspaceRoot = [&] {
        if (const auto* vfs = VirtualFileSystem::get()) {
            return vfs->getWorkingRoot();
        }
        std::error_code error;
        return std::filesystem::current_path(error);
    }();
    const fs::path exampleRoot = workspaceRoot / "Example";
    if (!fs::exists(exampleRoot) || !fs::is_directory(exampleRoot)) {
        _projectBrowserError = std::format("Project root not found: {}", exampleRoot.string());
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(exampleRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".yaproject") {
            continue;
        }
        _discoveredProjects.push_back(fs::weakly_canonical(entry.path()).string());
    }

    std::sort(_discoveredProjects.begin(), _discoveredProjects.end());
    if (!_discoveredProjects.empty()) {
        _projectBrowserSelection = 0;
    }
    else {
        _projectBrowserError = "No .yaproject files were found under Example/.";
    }
}

bool EditorLayer::openProjectInPlace(const std::string& projectPath)
{
    if (!_app) {
        _projectBrowserError = "App is not available.";
        return false;
    }

    try {
        const auto descriptor = FProjectDescriptor::load(projectPath);
        if (!_app->openProject(descriptor)) {
            _projectBrowserError = std::format("Failed to open project scene: {}", descriptor.name);
            return false;
        }
        _currentScenePath = descriptor.defaultScene.value_or(std::string{});
        _projectBrowserError.clear();
        return true;
    }
    catch (const std::exception& exception) {
        _projectBrowserError = exception.what();
        return false;
    }
}

void EditorLayer::setShowViewportCameraOverlay(bool enabled)
{
    _bShowViewportCameraOverlay = enabled;
    ConfigManager::Editor(kEditorConfigDocument)
        .set(kViewportCameraOverlayEnabledKey, enabled)
        .flush();
}

void EditorLayer::setViewportSamplerType(int samplerType)
{
    _viewPortSamplerType = samplerType == 1 ? Nearest : Linear;
}

void EditorLayer::setDefaultScenePathDraft(std::string path)
{
    strncpy_s(_defaultScenePathBuffer, sizeof(_defaultScenePathBuffer), path.c_str(), _TRUNCATE);
    _bDefaultScenePathDirty = true;
}

void EditorLayer::applyDefaultScenePathDraft()
{
    ConfigManager::Editor("editor").set("startup.defaultScenePath", std::string(_defaultScenePathBuffer)).flush();
    _bDefaultScenePathDirty = false;
}

void EditorLayer::resetDefaultScenePathDraft()
{
    syncEditorSettingsFromConfig();
}

bool EditorLayer::defaultScenePathExists() const
{
    const std::string scenePath = _defaultScenePathBuffer;
    if (scenePath.empty()) {
        return false;
    }
    return VFS::get() && VirtualFileSystem::get()->isFileExists(scenePath);
}

} // namespace ya
