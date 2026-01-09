#pragma once

#include "ContentBrowserPanel.h"
#include "Core/Event.h"
#include "Render/Core/DescriptorSet.h"
#include "SceneHierarchyPanel.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <memory>
#include <unordered_map>



namespace ya
{

struct ImGuiImageEntry
{
    stdptr<IImageView>  imageView;
    stdptr<Sampler>     sampler;
    DescriptorSetHandle ds;

    bool operator==(const ImGuiImageEntry &other) const
    {
        return imageView == other.imageView && sampler == other.sampler;
    }
    bool operator<(const ImGuiImageEntry &other) const
    {
        if (imageView != other.imageView)
            return imageView < other.imageView;
        return sampler < other.sampler;
    }
    operator bool() const
    {
        return imageView != nullptr && sampler != nullptr && ds != nullptr;
    }

    bool isValid() const
    {
        return this->operator bool();
    }

    operator ImTextureRef() const
    {
        return (ImTextureRef)ds.ptr;
    }
};

} // namespace ya

// Provide std::hash specialization for unordered_set support
namespace std
{
template <>
struct hash<ya::ImGuiImageEntry>
{
    size_t operator()(const ya::ImGuiImageEntry &entry) const noexcept
    {
        size_t h1 = hash<void *>{}(entry.sampler.get());
        size_t h2 = hash<void *>{}(entry.imageView.get());
        return h1 ^ (h2 << 1);
    }
};
} // namespace std

namespace ya
{

struct App;
struct EditorLayer
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
    Rect2D    viewportRect;
    bool      bViewportFocused = false;
    bool      bViewportHovered = false;

    // Editor settings
    glm::vec4 _clearColor = {0.1f, 0.1f, 0.1f, 1.0f};
    float     _debugFloat = 0.0f;

    // ImGui texture descriptor set cache (editor-only, application layer)
    std::unordered_set<ImGuiImageEntry> _imguiTextureCache; // ImageView → VkDescriptorSet

    std::function<void()> _contentFunc;

    // Gizmo state
    ImGuizmo::OPERATION _gizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      _gizmoMode = ImGuizmo::LOCAL;

    const ImGuiImageEntry *_playIcon       = nullptr;
    const ImGuiImageEntry *_pauseIcon      = nullptr;
    const ImGuiImageEntry *_stopIcon       = nullptr;
    const ImGuiImageEntry *_simulationIcon = nullptr;
    const ImGuiImageEntry *_viewportImage  = nullptr;

    void *_currentViewportImageHandle = nullptr; // Track ImageView handle to detect changes

    uint32_t _resizeTimerHandle = 0;

  public:
    Delegate<void(Rect2D /*rect*/)> onViewportResized;

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

    bool screenToViewport(float screenX, float screenY, float &outX, float &outY) const;
    bool screenToViewport(const glm::vec2 in, glm::vec2 &out) const;

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


    /**
     * @brief Get or create ImGui texture ID for rendering in ImGui::Image()
     * @param imageView Platform image view handle (e.g., VkImageView)
     * @param sampler Platform sampler handle (e.g., VkSampler)
     * @return ImTextureID (VkDescriptorSet as void*)
     */
    const ImGuiImageEntry *getOrCreateImGuiTextureID(stdptr<IImageView> imageView, stdptr<Sampler> sampler = nullptr);


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


    void cleanupImGuiTextures();
    void removeImGuiTexture(const ImGuiImageEntry *entry);
    void renderGizmo();

  public:
    // Public getters
    glm::vec2 getViewportSize() const { return _viewportSize; }
    bool      isViewportFocused() const { return bViewportFocused; }
    bool      isViewportHovered() const { return bViewportHovered; }
    bool      isGizmoActive() const; // Check if ImGuizmo is being used or hovered
    // void      setViewportImage(stdptr<IImageView> image) { _viewportImage = getOrCreateImGuiTextureID(image); }
};

} // namespace ya
