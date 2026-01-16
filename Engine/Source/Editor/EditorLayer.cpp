#include "EditorLayer.h"
#include "Core/App/App.h"
#include "Core/AssetManager.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/KeyCode.h"
#include "Core/Manager/Facade.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/RayCastMousePickingSystem.h"
#include "ImGuiHelper.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>



#include "Render/TextureLibrary.h"

#include "Core/Math/Math.h"

namespace ya
{

EditorLayer::EditorLayer(App *app)
    : _app(app),
      _sceneHierarchyPanel(this),
      _detailsView(this),
      _contentBrowserPanel(this)
{
}

void EditorLayer::onAttach()
{
    YA_PROFILE_FUNCTION();
    YA_CORE_INFO("EditorLayer::onAttach");

    if (!_app)
        return;

    // Subscribe to scene activation events to cleanup stale entity references
    // _app->getSceneManager()->onSceneActivated.addLambda(this, [this](Scene* newScene) {
    //     YA_CORE_INFO("EditorLayer: Scene activated, clearing selection");
    //     // Clear selection to avoid dangling pointer to destroyed entities
    //     _sceneHierarchyPanel.setSelection(nullptr);
    //     // Update hierarchy panel context
    //     _sceneHierarchyPanel.setContext(newScene);
    // });

    // Initialize editor panels
    if (auto scene = _app->getSceneManager()->getActiveScene())
    {
        _sceneHierarchyPanel.setContext(scene);
    }

    _contentBrowserPanel.init();

    // Initialize FilePicker with same icons as ContentBrowserPanel
    _filePicker.setIcons(_contentBrowserPanel.folderIcon, _contentBrowserPanel.fileIcon);
    _filePicker.setDefaultViewMode(FileExplorer::ViewMode::Icon);

    auto am             = AssetManager::get();
    auto playIcon       = am->loadTexture("play", "Engine/Content/TestTextures/editor/play.png");
    auto pauseIcon      = am->loadTexture("pause", "Engine/Content/TestTextures/editor/pause.png");
    auto stopIcon       = am->loadTexture("stop", "Engine/Content/TestTextures/editor/stop.png");
    auto simulationIcon = am->loadTexture("simulate_button", "Engine/Content/TestTextures/editor/simulate_button.png");

    // Validate texture loading
    if (!playIcon) YA_CORE_ERROR("Failed to load play icon");
    if (!pauseIcon) YA_CORE_ERROR("Failed to load pause icon");
    if (!stopIcon) YA_CORE_ERROR("Failed to load stop icon");
    if (!simulationIcon) YA_CORE_ERROR("Failed to load simulation icon");

    _playIcon       = getOrCreateImGuiTextureID(playIcon->getImageView());
    _pauseIcon      = getOrCreateImGuiTextureID(pauseIcon->getImageView());
    _stopIcon       = getOrCreateImGuiTextureID(stopIcon->getImageView());
    _simulationIcon = getOrCreateImGuiTextureID(simulationIcon->getImageView());

    // Subscribe to viewport framebuffer recreation to cleanup stale ImGui textures
    if (_app->_viewportRT) {
        _app->_viewportRT->onFrameBufferRecreated.addLambda(this, [this]() {
            YA_CORE_INFO("EditorLayer: Viewport framebuffer recreated, cleaning up cached viewport image");
            // Clear the cached viewport image since the old ImageView/DescriptorSet is now invalid
            if (_viewportImage && _viewportImage->ds) {
                removeImGuiTexture(_viewportImage);
                _viewportImage              = nullptr;
                _currentViewportImageHandle = nullptr;
            }
        });

        YA_CORE_INFO("EditorLayer attached successfully");
    }
}

void EditorLayer::onDetach()
{
    YA_CORE_INFO("EditorLayer::onDetach");
    // Unsubscribe from scene manager events
    if (_app && _app->getSceneManager()) {
        _app->getSceneManager()->onSceneActivated.removeAll(this);
    }
    // Cleanup ImGui textures before destroying panels
    cleanupImGuiTextures();
}

void EditorLayer::onUpdate(float dt)
{
    YA_PROFILE_FUNCTION();
    // Update logic here if needed
}



bool EditorLayer::screenToViewport(float screenX, float screenY, float &outX, float &outY) const
{
    // Check if point is within viewport bounds
    if (screenX < _viewportBounds[0].x || screenX > _viewportBounds[1].x ||
        screenY < _viewportBounds[0].y || screenY > _viewportBounds[1].y)
    {
        return false;
    }

    // Transform to viewport-local coordinates (0,0 at top-left of viewport)
    outX = screenX - _viewportBounds[0].x;
    outY = screenY - _viewportBounds[0].y;

    return true;
}

bool EditorLayer::screenToViewport(const glm::vec2 in, glm::vec2 &out) const
{
    return screenToViewport(in.x, in.y, out.x, out.y);
}

void EditorLayer::onEvent(const Event &event)
{
    // Handle viewport-specific events when focused
    // Example: Camera controls, object picking, gizmo manipulation

    if (!bViewportFocused) {
        return; // Only process other events when viewport is focused
    }

    // Example event handling (extend as needed):
    switch (event.getEventType()) {
    case EEvent::MouseMoved:
    {
    } break;

    case EEvent::MouseButtonPressed:
        break;
    case EEvent::MouseButtonReleased:
    {
        // Handle viewport clicks (object selection, gizmo interaction)
        auto &mouseEvent = static_cast<const MouseButtonPressedEvent &>(event);
        // Only pick on left click and when gizmo is not being used
        if (mouseEvent.GetMouseButton() == EMouse::Left) {
            if (!isGizmoActive()) {
                float localX{}, localY{};
                auto  cursorPos = _app->getLastMousePos();
                if (screenToViewport(cursorPos.x, cursorPos.y, localX, localY)) {
                    pickEntity(localX, localY);
                }
            }
        }
    } break;

    case EEvent::MouseScrolled:
    {
        // Handle camera zoom in viewport
    } break;

    case EEvent::KeyPressed:
    {
        auto &keyEvent = static_cast<const KeyPressedEvent &>(event);
        if (_selections.size() > 0 && _selections[0]->isValid())
        {
            // Handle viewport shortcuts (W/E/R for gizmo, Delete for selection, etc.)
            switch (keyEvent._keyCode) {
            case EKey::K_W:
                _gizmoOperation = ImGuizmo::TRANSLATE;
                break;
            case EKey::K_E:
                _gizmoOperation = ImGuizmo::ROTATE;
                break;
            case EKey::K_R:
                _gizmoOperation = ImGuizmo::SCALE;
                break;
            default:
                break;
            }
        }

        if (keyEvent.getKeyCode() == EKey::K_F) {
            // Focus camera on selected entity
            if (_sceneHierarchyPanel.getSelectedEntity()->isValid()) {
                focusCameraOnEntity(_sceneHierarchyPanel.getSelectedEntity());
            }
        }


    } break;

    default:
        break;
    }
}

void EditorLayer::updateWindowFlags()
{
    YA_PROFILE_FUNCTION();
    if (bFullscreen)
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    }
}

void EditorLayer::setupDockspace()
{
    YA_PROFILE_FUNCTION();
    ImGuiIO &io = ImGui::GetIO();

    ImGuiStyle &style          = ImGui::GetStyle();
    float       minWindowWidth = style.WindowMinSize.x;
    style.WindowMinSize.x      = 320.f;
    style.WindowMinSize.x      = minWindowWidth;

    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), _dockspaceFlags);
        // ImGui::DockSpaceOverViewport();
    }
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
                    auto *scene = _app->getSceneManager()->getActiveScene();
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
                    auto *scene = _app->getSceneManager()->getActiveScene();
                    if (scene && !scene->getName().empty())
                    {
                        defaultName = scene->getName();
                    }
                }

                _filePicker.openSceneSavePicker(
                    defaultName,
                    [this](const std::string &selectedDir, const std::string &sceneName) {
                        _currentScenePath = selectedDir + "/" + sceneName + ".scene.json";
                        if (_app && _app->getSceneManager())
                        {
                            auto *scene = _app->getSceneManager()->getActiveScene();
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
                auto *scene = _app->getSceneManager()->getActiveScene();
                if (scene && !scene->getName().empty())
                {
                    defaultName = scene->getName();
                }
            }

            _filePicker.openSceneSavePicker(
                defaultName,
                [this](const std::string &selectedDir, const std::string &sceneName) {
                    _currentScenePath = selectedDir + "/" + sceneName + ".scene.json";
                    if (_app && _app->getSceneManager())
                    {
                        auto *scene = _app->getSceneManager()->getActiveScene();
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.6f, 0.6f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.6f, 0.6f, 0.5f));

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
        _app->startRuntime();
        // TODO: Play scene
    }
    ImGui::SameLine();
    if (ImGui::ImageButton("Simulate", *_simulationIcon, ImVec2(size, size)))
    {
    }
    // if (ImGui::ImageButton("Pause", pauseDS, ImVec2(size * 2, size)))
    // {
    // }
    ImGui::SameLine();
    if (ImGui::ImageButton("Stop", *_stopIcon, ImVec2(size, size)))
    {
        _app->stopRuntime();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::End();
}

// void EditorLayer::settingsWindow()
// {
//     if (!ImGui::Begin("Settings", &bShowSettingsWindow))
//     {
//         ImGui::End();
//         return;
//     }

//     ImGui::ColorEdit4("Clear Color##editor", glm::value_ptr(_clearColor));
//     ImGui::DragFloat("Debug Float##editor", &_debugFloat, 0.01f);

//     ImGui::End();
// }


void EditorLayer::viewportWindow()
{
    YA_PROFILE_FUNCTION();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{460, 300});


    if (!ImGui::Begin("Viewport"))
    {
        ImGui::PopStyleVar(2);
        ImGui::End();
        return;
    }

    // Update focus/hover state
    bViewportFocused = ImGui::IsWindowFocused();
    bViewportHovered = ImGui::IsWindowHovered();

    // Get viewport panel size
    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();



    // Calculate viewport bounds for mouse picking
    auto   windowPos = ImGui::GetWindowPos();
    ImVec2 minBound  = ImGui::GetWindowContentRegionMin();
    ImVec2 maxBound  = ImGui::GetWindowContentRegionMax();
    minBound.x += windowPos.x;
    minBound.y += windowPos.y;
    maxBound.x += windowPos.x;
    maxBound.y += windowPos.y;

    _viewportBounds[0] = {minBound.x, minBound.y};
    _viewportBounds[1] = {maxBound.x, maxBound.y};


    // Update viewport size if changed
    if (_viewportSize.x != viewportPanelSize.x || _viewportSize.y != viewportPanelSize.y)
    {
        // if (_resizeTimerHandle != 0) {

        //
        //     Facade.timerManager.cancelTimer(_resizeTimerHandle);
        //     _resizeTimerHandle = 0;
        // }
        // else {
        // Always update size immediately for correct display
        _viewportSize = {viewportPanelSize.x, viewportPanelSize.y};

        // Rect2D rect = {
        //     .pos    = {_viewportBounds[0].x, _viewportBounds[0].y},
        //     .extent = {viewportPanelSize.x, viewportPanelSize.y},
        // };

        // _resizeTimerHandle = Facade.timerManager.delayCall(
        //     300,
        //     [this]() {
        // Facade.timerManager.cancelTimer(_resizeTimerHandle);
        // _resizeTimerHandle = 0;
        onViewportResized.executeIfBound(
            Rect2D{
                .pos    = _viewportBounds[0],
                .extent = {viewportPanelSize.x, viewportPanelSize.y},
            });
        YA_CORE_INFO("Viewport resized to: {} x {} (framebuffer rebuilt)", _viewportSize.x, _viewportSize.y);
        //         });
        // }
    }

    // Display the render texture from editor render target
    if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
    {
        auto *viewportRT = App::get()->_viewportRT.get();
        if (viewportRT && viewportRT->getFrameBuffer())
        {
            auto imageView = viewportRT->getFrameBuffer()->getImageView(0);
            if (imageView)
            {
                ImageViewHandle currentHandle  = imageView->getHandle();
                uint32_t        curFrameBuffer = viewportRT->getFrameBufferIndex();


                // Check if ImageView changed (framebuffer was recreated)
                if (_currentViewportImageHandle != currentHandle)
                {
                    // YA_CORE_TRACE("Viewport ImageView changed, refreshing ImGui texture: {}", App::get()->_frameIndex);

                    // Cleanup old cached texture if exists
                    if (_viewportImage && _viewportImage->ds) {
                        removeImGuiTexture(_viewportImage);
                    }

                    // Create new descriptor set for new ImageView
                    _viewportImage              = getOrCreateImGuiTextureID(imageView);
                    _currentViewportImageHandle = currentHandle;
                }

                // Render viewport image
                if (_viewportImage && _viewportImage->isValid())
                {
                    ImGui::Image(*_viewportImage, viewportPanelSize, ImVec2(0, 0), ImVec2(1, 1));

                    // Render gizmo overlay
                    renderGizmo();
                }
            }
        }
    }
    else
    {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Viewport: %.0f x %.0f", _viewportSize.x, _viewportSize.y);
    }

    if (ImGui::IsMouseClicked(1)) {
        ImGui::SetWindowFocus();
    }


    bViewportFocused = ImGui::IsWindowFocused();
    bViewportHovered = ImGui::IsWindowHovered();

    // Allow ImGuizmo to receive input even when viewport is focused
    // Block events only when viewport is NOT focused/hovered AND ImGuizmo is not using/over
    bool isGizmoActive = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
    ImGuiManager::get().setBlockEvents(!bViewportFocused && !bViewportHovered && !isGizmoActive);

    ImGui::PopStyleVar(2);
    ImGui::End();
}

const ImGuiImageEntry *EditorLayer::getOrCreateImGuiTextureID(stdptr<IImageView> imageView, stdptr<Sampler> sampler)
{
    YA_PROFILE_FUNCTION();
    if (!imageView) {
        YA_CORE_WARN("EditorLayer::getOrCreateImGuiTextureID: Invalid imageView or sampler");
        return nullptr;
    }
    if (!sampler) {
        sampler = TextureLibrary::get().getDefaultSampler();
    }

    ImGuiImageEntry entry{
        .imageView = imageView,
        .sampler   = sampler,
        .ds        = {},
    };
    // Check if already cached
    auto it = _imguiTextureCache.find(entry);
    if (it != _imguiTextureCache.end())
    {
        if (it->ds != nullptr) {
            return &(*it); // Return cached descriptor set
        }
    }

    // Create new ImGui descriptor set (platform-agnostic through ImGuiManager)
    void *textureID = ImGuiManager::addTexture(imageView.get(), sampler.get(), EImageLayout::ShaderReadOnlyOptimal);

    if (!textureID) {
        YA_CORE_ERROR("EditorLayer::getOrCreateImGuiTextureID: Failed to create descriptor set");
        return nullptr;
    }

    entry.ds = textureID;
    // Cache and return
    _imguiTextureCache.insert(entry);
    YA_CORE_TRACE("Created ImGui descriptor set for imageView: {}", imageView->getHandle().ptr);

    return &(*_imguiTextureCache.find(entry));
}

void EditorLayer::cleanupImGuiTextures()
{
    YA_CORE_INFO("EditorLayer::cleanupImGuiTextures - Releasing {} descriptor sets", _imguiTextureCache.size());

    for (auto &entry : _imguiTextureCache) {
        if (entry.ds) {
            ImGuiManager::removeTexture(entry.ds);
        }
    }
    _imguiTextureCache.clear();
}

void EditorLayer::removeImGuiTexture(const ImGuiImageEntry *entry)
{
    ImGuiManager::removeTexture(entry->ds);
    _imguiTextureCache.erase(*entry);
}

bool EditorLayer::isGizmoActive() const
{
    bool bUsing = ImGuizmo::IsUsing();
    bool bOver  = ImGuizmo::IsOver();
    return bUsing || bOver;
}

void EditorLayer::renderGizmo()
{
    YA_PROFILE_FUNCTION();
    // Get selected entity from hierarchy panel
    Entity *selectedEntity = _sceneHierarchyPanel.getSelectedEntity();

    // CRITICAL: Do NOT call selectedEntity->isValid() before null check!
    // The entity pointer may point to destroyed memory after scene switch.
    // The scene switch handler should have cleared selection, but double-check here.
    if (!selectedEntity) {
        ImGuizmo::Enable(false);
        return; // No entity selected
    }

    // Now safe to call member functions - verify entity is still valid
    if (!selectedEntity->isValid()) {
        YA_CORE_WARN("Selected entity is invalid after scene switch, clearing selection");
        _sceneHierarchyPanel.setSelection(nullptr);
        ImGuizmo::Enable(false);
        return;
    }

    ImGuizmo::Enable(true);

    // Get transform component
    if (!selectedEntity->hasComponent<TransformComponent>()) {
        return;
    }
    auto *tc = selectedEntity->getComponent<TransformComponent>();
    if (!tc) {
        return; // No transform component
    }

    // Get camera view and projection matrices
    auto *app = App::get();
    if (!app) {
        return;
    }

    glm::mat4 view = app->camera.getViewMatrix();
    glm::mat4 proj = app->camera.getProjectionMatrix();

    // Setup ImGuizmo context
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();

    // Set gizmo rect to match viewport bounds
    ImGuizmo::SetRect(
        _viewportBounds[0].x,
        _viewportBounds[0].y,
        _viewportSize.x,
        _viewportSize.y);

    // a copy for manipulation
    glm::mat4 transform = tc->getTransform();

    // Snap settings (can be toggled with Ctrl key)
    float snap[3] = {0.0f, 0.0f, 0.0f};     // No snap by default
    bool  useSnap = ImGui::GetIO().KeyCtrl; // Hold Ctrl to enable snap

    if (useSnap) {
        // Snap values for different operations
        switch (_gizmoOperation) {
        case ImGuizmo::TRANSLATE:
            snap[0] = snap[1] = snap[2] = 0.5f; // 0.5 unit snap for translation
            break;
        case ImGuizmo::ROTATE:
            snap[0] = snap[1] = snap[2] = 15.0f; // 15 degree snap for rotation
            break;
        case ImGuizmo::SCALE:
            snap[0] = snap[1] = snap[2] = 0.1f; // 0.1 snap for scale
            break;
        default:
            break;
        }
    }

    // Manipulate transform with gizmo
    if (ImGuizmo::Manipulate(
            glm::value_ptr(view),
            glm::value_ptr(proj),
            _gizmoOperation,
            _gizmoMode,
            glm::value_ptr(transform),
            nullptr,
            useSnap ? snap : nullptr))
    {
        // Gizmo was used, decompose matrix back to position/rotation/scale
        glm::vec3 position, rotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(transform),
            glm::value_ptr(position),
            glm::value_ptr(rotation),
            glm::value_ptr(scale));

        // Update transform component
        tc->_position = position;
        tc->_rotation = glm::radians(rotation); // ImGuizmo uses degrees
        tc->_scale    = scale;
        tc->bDirty    = true;
        // tc->setPosition(position);
        // tc->setRotation(glm::radians(rotation)); // ImGuizmo uses degrees
        // tc->setScale(scale);


        // YA_CORE_TRACE("Gizmo manipulated: pos({}, {}, {})", position.x, position.y, position.z);
    }
}

void EditorLayer::pickEntity(float viewportLocalX, float viewportLocalY)
{
    YA_PROFILE_FUNCTION_LOG();
    auto *app = App::get();
    if (!app) {
        return;
    }

    // Get active scene
    auto *scene = app->getSceneManager()->getActiveScene();
    if (!scene) {
        return;
    }
    glm::mat4 view{}, projection{};

    // Focus DO NOT support orbit camera for now
    // if (app->_viewportRT->isUseEntityCamera()) {
    //     auto *entityCam = app->_viewportRT->getCamera();
    //     YA_CORE_ASSERT(entityCam->isValid(), "Entity camera is null despite isUseEntityCamera being true");
    //     if (auto *camComp = entityCam->getComponent<CameraComponent>()) {
    //         view       = camComp->getOrbitView();
    //         projection = camComp->getProjection();
    //     }
    // }
    // else {
    view       = app->camera.getViewMatrix();
    projection = app->camera.getProjectionMatrix();
    // }

    // Use RayCastMousePickingSystem to pick entity
    // viewportLocalX/Y are in viewport space (0,0 = top-left of viewport)
    Entity *pickedEntity = RayCastMousePickingSystem::pickEntity(
        scene,
        viewportLocalX,
        viewportLocalY,
        _viewportSize.x,
        _viewportSize.y,
        view,
        projection);

    // Update selection
    if (pickedEntity) {
        _sceneHierarchyPanel.setSelection(pickedEntity);
        YA_CORE_INFO("Picked entity: {}", pickedEntity->getName());
    }
    else {
        _sceneHierarchyPanel.setSelection(nullptr);
        YA_CORE_INFO("No entity picked");
    }
}

glm::vec3 extractEulerAnglesFromViewMatrix(const glm::mat4 &viewMatrix)
{
    glm::mat3 rotMat  = glm::mat3(viewMatrix);
    glm::vec3 forward = -rotMat[2];
    glm::vec3 right   = rotMat[0];
    glm::vec3 up      = rotMat[1];

    // 计算 yaw（偏航，绕y轴）
    float yaw = atan2(forward.x, forward.z);
    // 计算 pitch（俯仰，绕x轴）
    float pitch = asin(forward.y);
    // 计算 roll（翻滚，绕z轴），此处因锁定worldUp，roll应为0
    float roll = atan2(-right.y, up.y);

    // 转换为角度制（若你的 Camera::setRotation 期望角度而非弧度，根据需求调整）
    return glm::vec3(glm::degrees(pitch), glm::degrees(yaw), glm::degrees(roll));
}

void EditorLayer::focusCameraOnEntity(Entity *entity)
{
    if (!entity || !entity->isValid()) {
        return;
    }
    auto *tc = entity->getComponent<TransformComponent>();
    if (!tc) {
        return;
    }
    auto *app = App::get();
    if (!app) {
        return;
    }
    float distance = 10.0f; // Fixed distance from entity, can be adjusted

    glm::vec3 entityPos = tc->getPosition();
    glm::vec3 camPos    = app->camera.getPosition();

    glm::vec3 camToEntity = glm::normalize(entityPos - camPos);

    // Position camera behind entity at fixed distance
    glm::vec3 newCamPos = entityPos - camToEntity * distance;

    glm::vec3 newCamRotation{};
    {
        glm::vec3 newCamToEntity = glm::normalize(entityPos - newCamPos);
        // y 为 dir 与 xoz 平面的夹角的正弦值 sin(theta), arcsin(sin(theta)) 得到角度值 theta, 即 pitch
        float pitch = glm::degrees(std::asin(newCamToEntity.y));


        glm::vec2 xozPlane = glm::vec2{FMath::Vector::WorldRight.x, FMath::Vector::WorldForward.z};
        (void)xozPlane;
        //  现在我们在 xoz 平面上, 想象一个平面坐标系, dir.x 向右， dir.z 向上(1),  yaw 即为角度值
        // tan(theta) = y/x, 即通过角度求斜率, atan(y/x) 会丢失象限信息(如45度与135度), 即单独的 dir.x 和 dir.z 无法确定正确的角度
        // 使用 atan2(y,x) 可以保留象限信息, 通过 dir.x 和 dir.z 的正负号确定正确的角度
        // atan2(1,1) = 45度, atan2(1,-1)=135度, atan2(-1,-1)=-135度, atan2(-1,1)=-45度

        // 注意这里 direction.z 应该取反，因为在右手坐标系中，Z 轴正方向是向屏幕外侧的，
        // 这样 xoz 就与 屏幕坐标系的 y 向上相反，所以整个方向旋转 180 度(单独z相反会导致左右相反)
        if constexpr (FMath::Vector::IsRightHanded) {
            newCamToEntity.z = -newCamToEntity.z;
            newCamToEntity.x = -newCamToEntity.x; // 否则会左右相反
        }
        float yaw = glm::degrees(std::atan2(newCamToEntity.x, newCamToEntity.z));


        // float pitch    = glm::degrees(std::atan2(newCamToEntity.y, newCamToEntity.z));
        newCamRotation = glm::vec3(pitch, yaw, 0.0f);
    }


    app->camera.setPosition(newCamPos);
    app->camera.setRotation(newCamRotation);
}

} // namespace ya
