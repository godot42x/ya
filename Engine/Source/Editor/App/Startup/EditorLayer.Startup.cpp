#include "Editor/App/EditorLayerInternal.h"

namespace ya
{
Scene* EditorLayer::getEditableScene() const
{
    return _editableScene;
}

Scene* EditorLayer::getViewportInteractionScene() const
{
    if (!_app) {
        return nullptr;
    }

    auto* sceneManager = _app->getSceneManager();
    if (!sceneManager) {
        return nullptr;
    }

    return sceneManager->getActiveScene();
}

void EditorLayer::syncEditorSettingsFromConfig()
{
    const std::string defaultScenePath = ConfigManager::get().getOr<std::string>("editor", "startup.defaultScenePath", "");
    strncpy_s(_defaultScenePathBuffer, sizeof(_defaultScenePathBuffer), defaultScenePath.c_str(), _TRUNCATE);
    _bDefaultScenePathDirty = false;
    _bShowViewportCameraOverlay = ConfigManager::get().getOr<bool>("editor",
                                                                   "viewport.cameraOverlay.enabled",
                                                                   _bShowViewportCameraOverlay);
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
    const fs::path exampleRoot = fs::current_path() / "Example";
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
        _projectBrowserError.clear();
        return true;
    }
    catch (const std::exception& exception) {
        _projectBrowserError = exception.what();
        return false;
    }
}

void EditorLayer::editorSettings()
{
    if (!ImGui::Begin("Editor Settings")) {
        ImGui::End();
        return;
    }

    if (!_bDefaultScenePathDirty && _defaultScenePathBuffer[0] == '\0') {
        syncEditorSettingsFromConfig();
    }

    ImGui::Combo("Viewport Sampler", (int*)&_viewPortSamplerType, "Linear\0Nearest\0");
    if (ImGui::Checkbox("Show Viewport Camera Overlay", &_bShowViewportCameraOverlay)) {
        ConfigManager::Editor(kEditorConfigDocument)
            .set(kViewportCameraOverlayEnabledKey, _bShowViewportCameraOverlay)
            .flush();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Startup Scene");
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##DefaultScenePath", _defaultScenePathBuffer, sizeof(_defaultScenePathBuffer))) {
        _bDefaultScenePathDirty = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        _filePicker.open(
            "Select Default Scene",
            _defaultScenePathBuffer,
            {".scene.json"},
            [this](const std::string& newPath) {
                strncpy_s(_defaultScenePathBuffer, sizeof(_defaultScenePathBuffer), newPath.c_str(), _TRUNCATE);
                _bDefaultScenePathDirty = true;
            });
    }

    const std::string scenePath = _defaultScenePathBuffer;
    const bool        bHasScenePath = !scenePath.empty();
    if (bHasScenePath) {
        const bool bExists = VFS::get() && VirtualFileSystem::get()->isFileExists(scenePath);
        ImGui::TextDisabled("Used on next app start");
        ImGui::TextColored(bExists ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f) : ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                           bExists ? "Scene exists" : "Scene not found");
    }
    else {
        ImGui::TextDisabled("Empty means startup falls back to an empty scene");
    }

    if (_bDefaultScenePathDirty) {
        if (ImGui::Button("Apply Default Scene Path")) {
            ConfigManager::Editor("editor").set("startup.defaultScenePath", std::string(_defaultScenePathBuffer));
            _bDefaultScenePathDirty = false;
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            syncEditorSettingsFromConfig();
        }
    }

    ImGui::Separator();
    if (ImGui::TreeNode("ImGui")) {
        if (ImGuiManager::get().onRenderGUI()) {
            saveImGuiSettingsToConfig();
        }
        ImGui::TreePop();
    }

    ImGui::End();
}
} // namespace ya
