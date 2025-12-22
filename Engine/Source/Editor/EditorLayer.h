#pragma once

#include "ContentBrowserPanel.h"
#include "Core/Event.h"
#include "SceneHierarchyPanel.h"
#include <imgui.h>
#include <memory>
#include <unordered_map>


namespace ya
{

struct App;

class EditorLayer
{
  private:
    App *_app = nullptr;

    // Editor panels
    SceneHierarchyPanel _sceneHierarchyPanel;
    ContentBrowserPanel _contentBrowserPanel;

    // ImGui Docking state
    ImGuiDockNodeFlags _dockspaceFlags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags   _windowFlags    = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    // Window state
    bool bFullscreen     = true;
    bool bPadding        = true;
    bool bShowDemoWindow = false;
    // bool bShowSettingsWindow = true;
    // bool bShowRenderStats    = true;

    // Viewport state
    glm::vec2 _viewportSize = {1280.f, 720.f};
    glm::vec2 _viewportBounds[2]; // Min and max bounds
    bool      bViewportFocused = false;
    bool      bViewportHovered = false;

    // Editor settings
    glm::vec4 _clearColor = {0.1f, 0.1f, 0.1f, 1.0f};
    float     _debugFloat = 0.0f;

    // ImGui texture descriptor set cache (editor-only, application layer)
    std::unordered_map<void *, void *> _imguiTextureCache; // ImageView → VkDescriptorSet

    std::function<void()> _contentFunc;

  public:
    EditorLayer(App *app);
    ~EditorLayer() = default;

    void onAttach();
    void onDetach();

    void onUpdate(float dt);
    void setSceneContext(Scene *scene)
    {
        _sceneHierarchyPanel.setContext(scene);
    }

    /**
     * @brief Check if viewport should capture input events
     * @return true if viewport is focused and should receive events
     */
    bool shouldCaptureInput() const { return bViewportFocused; }

    /**
     * @brief Transform screen coordinates to viewport local coordinates
     * @param screenX Screen X coordinate
     * @param screenY Screen Y coordinate
     * @param outX Output viewport-local X
     * @param outY Output viewport-local Y
     * @return true if coordinate is within viewport bounds
     */
    bool screenToViewport(float screenX, float screenY, float &outX, float &outY) const;

    void onImGuiRender(auto content)
    {
        updateWindowFlags();

        // Begin main dockspace window
        ImGui::Begin("MainEditor", nullptr, _windowFlags);

        if (bFullscreen) {
            ImGui::PopStyleVar(2);
        }
        if (!bPadding) {
            ImGui::PopStyleVar();
        }

        setupDockspace();
        menuBar();
        toolbar();
        viewportWindow();

        _sceneHierarchyPanel.onImGuiRender();
        _contentBrowserPanel.onImGuiRender();

        // Render windows
        // settingsWindow();
        // renderStatsWindow();

        content();

        // Demo window option
        if (bShowDemoWindow)
        {
            ImGui::ShowDemoWindow(&bShowDemoWindow);
        }

        ImGui::End(); // End main dockspace window
    }
    void onEvent(const Event &event);

    void setContent(std::function<void()> contentFunc)
    {
        _contentFunc = contentFunc;
    }



  private:
    // UI Methods
    void updateWindowFlags();
    void menuBar();
    void toolbar();
    // void settingsWindow();
    // void renderStatsWindow();
    void viewportWindow();

    // Helpers
    void setupDockspace();

    /**
     * @brief Get or create ImGui texture ID for rendering in ImGui::Image()
     * @param imageView Platform image view handle (e.g., VkImageView)
     * @param sampler Platform sampler handle (e.g., VkSampler)
     * @return ImTextureID (VkDescriptorSet as void*)
     */
    void *getOrCreateImGuiTextureID(void *imageView, void *sampler);

    /**
     * @brief Cleanup all ImGui texture descriptor sets
     */
    void cleanupImGuiTextures();

  public:
    // Public getters
    glm::vec2 getViewportSize() const { return _viewportSize; }
    bool      isViewportFocused() const { return bViewportFocused; }
    bool      isViewportHovered() const { return bViewportHovered; }
};

} // namespace ya
