#include "Editor/EditorLayerInternal.h"
#include "Editor/Panels/RuntimeToolsPanelInternal.h"

namespace ya
{
void renderFrameStatsContent(const App& app, float dt)
{
    static constexpr size_t kHistorySize = 120;
    static std::array<float, kHistorySize> fpsHistory{};
    static size_t historyHead = 0;
    static size_t historyFill = 0;
    static float  fpsSum      = 0.0f;

    const float fps = dt > 0.0f ? 1.0f / dt : 0.0f;
    if (historyFill >= kHistorySize) {
        fpsSum -= fpsHistory[historyHead];
    }
    fpsHistory[historyHead] = fps;
    fpsSum += fps;
    historyHead = (historyHead + 1) % kHistorySize;
    historyFill = std::min(historyFill + 1, kHistorySize);

    const float avgFps = historyFill > 0 ? fpsSum / static_cast<float>(historyFill) : 0.0f;

    ImGui::Text("Frame Index: %llu", static_cast<unsigned long long>(app.getFrameIndex()));
    ImGui::Text("Delta: %.2f ms", dt * 1000.0f);
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Avg FPS: %.1f", avgFps);
}

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
        ImGuiDockNodeFlags flags = _dockspaceFlags;
        if (_app && _app->getInputRouter().isMouseCaptured())
        {
            // While the game holds the mouse (PIE input capture) the editor
            // layout is locked: splitter drags must not resize the viewport
            // under a running game. Releasing capture restores layout edits.
            flags |= ImGuiDockNodeFlags_NoResize;
        }
        ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), flags);
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
        if (ImGui::MenuItem("Open Scene", "Ctrl+O"))
        {
            // TODO: Open scene
        }
        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
        {
            // Save to current path if available, otherwise open Save As dialog
            if (!_currentScenePath.empty())
            {
                if (_app && _app->getSceneServices().getSceneManager())
                {
                    auto* scene = getEditableScene();
                    if (scene)
                    {
                        _app->getSceneServices().saveScene(_currentScenePath);
                        YA_CORE_INFO("Scene saved to: {}", _currentScenePath);
                    }
                }
            }
            else
            {
                // No current path, open Save As dialog with scene save picker
                std::string defaultName = "NewScene";
                if (_app && _app->getSceneServices().getSceneManager())
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
                        if (_app && _app->getSceneServices().getSceneManager())
                        {
                            auto* scene = getEditableScene();
                            if (scene)
                            {
                                scene->setName(sceneName);
                                _app->getSceneServices().saveScene(_currentScenePath);
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
            if (_app && _app->getSceneServices().getSceneManager())
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
                    if (_app && _app->getSceneServices().getSceneManager())
                    {
                        auto* scene = getEditableScene();
                        if (scene)
                        {
                            scene->setName(sceneName);
                            _app->getSceneServices().saveScene(_currentScenePath);
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
        ImGui::MenuItem("Render Graph", nullptr, &bShowRenderGraphWindow);

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
    style.pushVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(6, 0));
    style.pushVar(ImGuiStyleVar_FrameRounding, 6.0f);

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
    const bool bEditMode     = _app->isStopped();
    const bool bRuntimeMode  = _app->isRuntimeMode();
    const bool bSimMode      = _app->isSimulationMode();
    const char* modeLabel    = bRuntimeMode ? "PLAYING" : (bSimMode ? "SIMULATING" : "EDIT");
    const ImVec4 activeColor = bRuntimeMode ? ImVec4(0.24f, 0.62f, 0.34f, 0.95f)
                                            : (bSimMode ? ImVec4(0.72f, 0.54f, 0.19f, 0.95f)
                                                        : ImVec4(0.23f, 0.47f, 0.85f, 0.95f));

    const float totalWidth = size * 3.0f + 16.0f + 88.0f;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowContentRegionMax().x - totalWidth) * 0.5f));

    auto drawModeButton = [&](const char* id, const ImGuiImageEntry& icon, bool bActive, const char* tooltip) {
        ya::ImGuiStyleScope buttonStyle;
        buttonStyle.pushColor(ImGuiCol_Button, bActive ? activeColor : ImVec4(0.22f, 0.22f, 0.24f, 0.92f));
        buttonStyle.pushColor(ImGuiCol_ButtonHovered, bActive ? activeColor : ImVec4(0.32f, 0.32f, 0.35f, 0.95f));
        buttonStyle.pushColor(ImGuiCol_ButtonActive, bActive ? activeColor : ImVec4(0.18f, 0.18f, 0.2f, 0.95f));
        const bool clicked = ImGui::ImageButton(id, icon, ImVec2(size, size));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return clicked;
    };

    if (drawModeButton("Play", *_playIcon, bRuntimeMode, "Play the current scene"))
    {
        _sceneHierarchyPanel.setSelection(nullptr);
        if (_app->isStopped()) {
            App::get()->getTaskManager().registerFrameTask([app = _app]() {
                if (app && app->isStopped()) {
                    app->startRuntime();
                }
            });
        }
    }
    ImGui::SameLine();
    if (drawModeButton("Simulate", *_simulationIcon, bSimMode, "Simulate without entering full runtime"))
    {
        _sceneHierarchyPanel.setSelection(nullptr);
        if (_app->isStopped()) {
            App::get()->getTaskManager().registerFrameTask([app = _app]() {
                if (app && app->isStopped()) {
                    app->startSimulation();
                }
            });
        }
    }
    ImGui::SameLine();
    if (drawModeButton("Stop", *_stopIcon, bEditMode, "Stop play/simulate and return to edit mode"))
    {
        App::get()->getTaskManager().registerFrameTask([app = _app]() {
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

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(8.0f, 0.0f));
    ImGui::SameLine();

    {
        ya::ImGuiStyleScope badgeStyle;
        badgeStyle.pushColor(ImGuiCol_Button, activeColor);
        badgeStyle.pushColor(ImGuiCol_ButtonHovered, activeColor);
        badgeStyle.pushColor(ImGuiCol_ButtonActive, activeColor);
        ImGui::Button(modeLabel, ImVec2(80.0f, size));
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(8.0f, 0.0f));
    ImGui::SameLine();

    // 2D / 3D are separate viewport entries (Godot-style): clicking "2D"
    // opens the UI canvas editor, clicking "3D" returns to the world view.
    // Development-time viewing only - the game always renders 3D + 2D
    // together (CLI: viewport.set_mode).
    auto drawModeEntry = [&](const char* label, EViewportMode mode, const char* tooltip) {
        const bool bActive = _viewportMode == mode;
        ya::ImGuiStyleScope modeStyle;
        modeStyle.pushColor(ImGuiCol_Button, bActive ? ImVec4(0.28f, 0.52f, 0.32f, 0.95f)
                                                     : ImVec4(0.22f, 0.22f, 0.24f, 0.92f));
        modeStyle.pushColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.62f, 0.40f, 0.95f));
        if (ImGui::Button(label, ImVec2(40.0f, size))) {
            setViewportMode(mode);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", tooltip);
        }
    };
    drawModeEntry("3D", EViewportMode::Mode3D, "3D world view (edit the scene)");
    ImGui::SameLine();
    drawModeEntry("2D", EViewportMode::Mode2D, "2D canvas view (Game UI designer preview)");

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

void renderGUIWorkbenchWindow(EditorLayer& layer)
{
    layer.getGUIWorkbenchPanel().onImGuiRender();
}

void EditorLayer::renderGraphWindow()
{
    if (!_app || !bShowRenderGraphWindow) {
        return;
    }
    renderRenderGraphWindowContent(*_app, &bShowRenderGraphWindow);
}

void EditorLayer::statsWindow()
{
    if (!ImGui::Begin("Frame Stats")){
        ImGui::End();
        return;
    }

    renderFrameStatsContent(*_app, _lastDeltaTime);
    ImGui::End();
}

void EditorLayer::renderAuxiliaryUi()
{
    renderGUIWorkbenchWindow(*this);
    _filePicker.render();
    renderGraphWindow();

    if (bShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&bShowDemoWindow);
    }
}
} // namespace ya
