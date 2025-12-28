#include "EditorLayer.h"
#include "Core/App/App.h"
#include "Core/AssetManager.h"
#include "Core/Manager/Facade.h"
#include "ImGuiHelper.h"
#include "Scene/Scene.h"
#include <glm/gtc/type_ptr.hpp>


#include "Render/TextureLibrary.h"


namespace ya
{

EditorLayer::EditorLayer(App *app)
    : _app(app),
      _sceneHierarchyPanel(this),
      _contentBrowserPanel(this)
{
}

void EditorLayer::onAttach()
{
    YA_CORE_INFO("EditorLayer::onAttach");

    if (!_app)
        return;

    // Initialize editor panels
    if (auto scene = _app->getScene())
    {
        _sceneHierarchyPanel.setContext(scene);
    }

    _contentBrowserPanel.init();


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
        // Cleanup ImGui textures before destroying panels
        cleanupImGuiTextures();
    }

    void EditorLayer::onUpdate(float dt)
    {
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
            return; // Only process events when viewport is focused
        }

        // Example event handling (extend as needed):
        switch (event.getEventType()) {
        case EEvent::MouseMoved:
        {
            // auto& mouseEvent = static_cast<const MouseMoveEvent&>(event);
            // float localX, localY;
            // if (screenToViewport(mouseEvent._x, mouseEvent._y, localX, localY)) {
            //     // Handle mouse hover for object picking, gizmo highlighting, etc.
            // }
        } break;

        case EEvent::MouseButtonPressed:
        {
            // Handle viewport clicks (object selection, gizmo interaction)
        } break;

        case EEvent::MouseScrolled:
        {
            // Handle camera zoom in viewport
        } break;

        case EEvent::KeyPressed:
        {
            // Handle viewport shortcuts (W/A/S/D for camera, Delete for selection, etc.)
        } break;

        default:
            break;
        }
    }

    void EditorLayer::updateWindowFlags()
    {
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
                // TODO: Save scene
            }
            if (ImGui::MenuItem("Save Scene As", "Ctrl+Shift+S"))
            {
                // TODO: Save scene as
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
            _app->requestQuit();
            // TODO: Play scene
        }
        ImGui::SameLine();
        if (ImGui::ImageButton("Simulate", *_simulationIcon, ImVec2(size, size)))
        {
        }
        // if (ImGui::ImageButton("Pause", pauseDS, ImVec2(size * 2, size)))
        // {
        // }
        if (ImGui::ImageButton("Stop", *_stopIcon, ImVec2(size, size)))
        {
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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{128, 128});

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
                    }
                }
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Viewport: %.0f x %.0f", _viewportSize.x, _viewportSize.y);
        }

        bViewportFocused = ImGui::IsWindowFocused();
        bViewportHovered = ImGui::IsWindowHovered();
        ImGuiManager::get().setBlockEvents(!bViewportFocused && !bViewportHovered);

        ImGui::PopStyleVar(2);
        ImGui::End();
    }

    const ImGuiImageEntry *EditorLayer::getOrCreateImGuiTextureID(stdptr<IImageView> imageView, stdptr<Sampler> sampler)
    {
        if (!imageView) {
            YA_CORE_WARN("EditorLayer::getOrCreateImGuiTextureID: Invalid imageView or sampler");
            return nullptr;
        }
        if (!sampler) {
            sampler = TextureLibrary::getDefaultSampler();
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

} // namespace ya
