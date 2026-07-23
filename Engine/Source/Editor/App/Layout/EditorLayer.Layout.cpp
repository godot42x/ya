#include "Editor/App/EditorLayerInternal.h"

namespace ya
{
void EditorLayer::updateWindowFlags(ya::ImGuiStyleScope& style)
{
    YA_PROFILE_FUNCTION();
    if (bFullscreen)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        style.pushVar(ImGuiStyleVar_WindowRounding, 0.0f);
        style.pushVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        _windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        _windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }
    else
    {
        _dockspaceFlags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
    }

    if (_dockspaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
    {
        _windowFlags |= ImGuiWindowFlags_NoBackground;
    }

    if (!bPadding)
    {
        style.pushVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    }
}

void EditorLayer::setupDockspace()
{
    YA_PROFILE_FUNCTION();
    ImGuiIO& io = ImGui::GetIO();

    ImGuiStyle& style     = ImGui::GetStyle();
    float       savedMinW = style.WindowMinSize.x;
    style.WindowMinSize.x = 320.f;

    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), _dockspaceFlags);
    }

    style.WindowMinSize.x = savedMinW;
}


void EditorLayer::menuBar()
{
    YA_PROFILE_FUNCTION();
    ImGui::BeginMenuBar();

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Scene", "Ctrl+N"))
        {
            // TODO: New scene
            App::get()->taskManager.registerFrameTask([]() {
                auto* app = App::get();
                if (!app) {
                    return;
                }

                auto* sceneManager = app->getSceneManager();
                if (sceneManager && sceneManager->hasScene()) {
                    if (auto* render = app->getRender()) {
                        render->waitIdle();
                    }
                }
                auto scene = makeShared<Scene>();
                if (sceneManager) {
                    sceneManager->unloadScene();
                    sceneManager->activateScene(scene);
                }
            });
        }
        if (ImGui::MenuItem("Open Scene", "Ctrl+O"))
        {
            // TODO: Open scene
        }
        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
        {
            // Save to current path if available, otherwise open Save As dialog
            if (!_currentScenePath.empty())
            {
                if (_app && _app->getSceneManager())
                {
                    auto* scene = getEditableScene();
                    if (scene)
                    {
                        _app->getSceneManager()->serializeToFile(_currentScenePath, scene);
                        YA_CORE_INFO("Scene saved to: {}", _currentScenePath);
                    }
                }
            }
            else
            {
                // No current path, open Save As dialog with scene save picker
                std::string defaultName = "NewScene";
                if (_app && _app->getSceneManager())
                {
                    auto* scene = getEditableScene();
                    if (scene && !scene->getName().empty())
                    {
                        defaultName = scene->getName();
                    }
                }

                _filePicker.openSceneSavePicker(
                    defaultName,
                    [this](const std::string& selectedDir, const std::string& sceneName) {
                        _currentScenePath = selectedDir + "/" + sceneName + ".scene.json";
                        if (_app && _app->getSceneManager())
                        {
                            auto* scene = getEditableScene();
                            if (scene)
                            {
                                scene->setName(sceneName);
                                _app->getSceneManager()->serializeToFile(_currentScenePath, scene);
                                YA_CORE_INFO("Scene saved to: {}", _currentScenePath);
                            }
                        }
                    });
            }
        }
        if (ImGui::MenuItem("Save Scene As", "Ctrl+Shift+S"))
        {
            // Open scene save picker with name input and mount point selection
            std::string defaultName = "NewScene";
            if (_app && _app->getSceneManager())
            {
                auto* scene = getEditableScene();
                if (scene && !scene->getName().empty())
                {
                    defaultName = scene->getName();
                }
            }

            _filePicker.openSceneSavePicker(
                defaultName,
                [this](const std::string& selectedDir, const std::string& sceneName) {
                    _currentScenePath = selectedDir + "/" + sceneName + ".scene.json";
                    if (_app && _app->getSceneManager())
                    {
                        auto* scene = getEditableScene();
                        if (scene)
                        {
                            scene->setName(sceneName);
                            _app->getSceneManager()->serializeToFile(_currentScenePath, scene);
                            YA_CORE_INFO("Scene saved to: {}", _currentScenePath);
                        }
                    }
                });
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Exit"))
        {
            if (_app)
            {
                _app->requestQuit();
            }
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Fullscreen", nullptr, &bFullscreen);
        ImGui::MenuItem("Padding", nullptr, &bPadding);
        ImGui::MenuItem("Show Demo Window", nullptr, &bShowDemoWindow);

        ImGui::Separator();

        if (ImGui::MenuItem("Flag: NoDockingOverCentralNode", nullptr, ((_dockspaceFlags & ImGuiDockNodeFlags_NoDockingOverCentralNode) != 0)))
        {
            _dockspaceFlags ^= ImGuiDockNodeFlags_NoDockingOverCentralNode;
        }

        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void EditorLayer::toolbar()
{
    YA_PROFILE_FUNCTION();

    ya::ImGuiStyleScope style;
    style.pushVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
    style.pushVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
    style.pushColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.3f));
    style.pushColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.6f, 0.6f, 0.5f));
    style.pushColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.6f, 0.6f, 0.5f));

    if (!ImGui::Begin("##toolbar",
                      nullptr,
                      ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoDecoration |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoResize))
    {
        ImGui::End();
        return;
    }


    float size = ImGui::GetWindowHeight() - 4.0f;

    ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 0.5f));

    if (ImGui::ImageButton("Play", *_playIcon, ImVec2(size, size)))
    {
        _sceneHierarchyPanel.setSelection(nullptr);
        if (_app->isStopped()) {
            App::get()->taskManager.registerFrameTask([app = _app]() {
                if (app && app->isStopped()) {
                    app->startRuntime();
                }
            });
        }
    }
    ImGui::SameLine();
    if (ImGui::ImageButton("Simulate", *_simulationIcon, ImVec2(size, size)))
    {
        _sceneHierarchyPanel.setSelection(nullptr);
        if (_app->isStopped()) {
            App::get()->taskManager.registerFrameTask([app = _app]() {
                if (app && app->isStopped()) {
                    app->startSimulation();
                }
            });
        }
    }
    ImGui::SameLine();
    if (ImGui::ImageButton("Stop", *_stopIcon, ImVec2(size, size)))
    {
        App::get()->taskManager.registerFrameTask([app = _app]() {
            if (!app) {
                return;
            }
            if (app->isRuntimeMode()) {
                app->stopRuntime();
            }
            else if (app->isSimulationMode()) {
                app->stopSimulation();
            }
        });
    }

    // style RAII auto-pop
    ImGui::End();
}

void EditorLayer::projectBrowserWindow()
{
    YA_PROFILE_FUNCTION();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

    if (!ImGui::Begin("Project Browser",
                      nullptr,
                      ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoDocking |
                          ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::End();
        return;
    }

    const ImVec2 workSize = viewport->WorkSize;
    const float  contentWidth = std::min(workSize.x - 80.0f, 920.0f);
    const float  topPadding   = std::max(32.0f, workSize.y * 0.12f);

    ImGui::SetCursorPos(ImVec2((workSize.x - contentWidth) * 0.5f, topPadding));
    ImGui::BeginGroup();

    ImGui::Text("YA Editor");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextWrapped("Select a project to open. This startup screen is isolated from the main editor layout until a project is loaded.");
    ImGui::Spacing();

    if (!_projectBrowserError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", _projectBrowserError.c_str());
        ImGui::Spacing();
    }

    const float buttonWidth = 160.0f;
    if (ImGui::Button("Refresh Projects", ImVec2(buttonWidth, 0.0f))) {
        refreshProjectBrowser();
    }
    ImGui::SameLine();
    if (ImGui::Button("Exit Editor", ImVec2(buttonWidth, 0.0f)) && _app) {
        _app->requestQuit();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float listHeight = std::max(240.0f, std::min(420.0f, workSize.y * 0.45f));
    if (ImGui::BeginListBox("##projects", ImVec2(contentWidth, listHeight))) {
        for (int index = 0; index < static_cast<int>(_discoveredProjects.size()); ++index) {
            const bool bSelected = _projectBrowserSelection == index;
            if (ImGui::Selectable(_discoveredProjects[index].c_str(), bSelected)) {
                _projectBrowserSelection = index;
            }
            if (bSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndListBox();
    }

    const bool bCanOpen = _projectBrowserSelection >= 0 &&
                          _projectBrowserSelection < static_cast<int>(_discoveredProjects.size());
    if (bCanOpen) {
        ImGui::TextWrapped("%s", _discoveredProjects[_projectBrowserSelection].c_str());
    }
    else {
        ImGui::TextDisabled("Select a project to continue.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Open Project", ImVec2(buttonWidth, 0.0f)) && bCanOpen) {
        const std::string selectedProject = _discoveredProjects[_projectBrowserSelection];
        openProjectInPlace(selectedProject);
    }

    ImGui::EndGroup();
    ImGui::End();
}

void EditorLayer::runtimeToolsWindow()
{
    if (!_app) {
        return;
    }
    _runtimeToolsPanel.onImGuiRender(*_app, _lastDeltaTime);
}

void EditorLayer::renderAuxiliaryUi()
{
    _filePicker.render();

    if (bShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&bShowDemoWindow);
    }
}
} // namespace ya
