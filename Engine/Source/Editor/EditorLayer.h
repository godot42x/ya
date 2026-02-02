#pragma once

#include "ContentBrowserPanel.h"
#include "Core/Base.h"

#include "Core/Event.h"
#include "DetailsView.h"
#include "FilePicker.h"
#include "ImGuiHelper.h"
#include "Render/Core/Image.h"
#include "SceneHierarchyPanel.h"
#include <imgui.h>

#include "Editor/EditorCommon.h"

#include <ImGuizmo.h>



namespace ya
{

struct App;
struct EditorLayer
{
  private:
    App                  *_app = nullptr;
    std::vector<Entity *> _selections;

    // Editor panels
    SceneHierarchyPanel _sceneHierarchyPanel;
    DetailsView         _detailsView;
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
    bool      bViewportFocused     = false;
    bool      bViewportHovered     = false;
    bool      _bRightMouseDragging = false; // Track right mouse drag for camera rotation
    glm::vec2 _rightMousePressPos  = {};    // Position when right mouse was pressed

    // Editor settings
    glm::vec4 _clearColor = {0.1f, 0.1f, 0.1f, 1.0f};
    float     _debugFloat = 0.0f;

    // ImGui texture descriptor set cache (editor-only, application layer)
    std::unordered_set<ImGuiImageEntry> _imguiTextureCache; // ImageView → VkDescriptorSet

    std::function<void()> _contentFunc;

    // Gizmo state
    ImGuizmo::OPERATION _gizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      _gizmoMode      = ImGuizmo::LOCAL;

    const ImGuiImageEntry *_playIcon       = nullptr;
    const ImGuiImageEntry *_pauseIcon      = nullptr;
    const ImGuiImageEntry *_stopIcon       = nullptr;
    const ImGuiImageEntry *_simulationIcon = nullptr;
    const ImGuiImageEntry *_viewportImage  = nullptr;

    void *_currentViewportImageHandle = nullptr; // Track ImageView handle to detect changes

    uint32_t _resizeTimerHandle = 0;
    Rect2D   _pendingViewportRect;    // Pending resize event to be processed in next frame
    bool     _bViewportResizePending = false;

  public:
    Delegate<void(Rect2D /*rect*/)> onViewportResized;

    // File picker for save/load dialogs and asset selection
    FilePicker  _filePicker;
    std::string _currentScenePath; // Current scene file path

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

    // Entity selection bus - notifies DetailsView of selection changes
    void setSelectedEntity(Entity *entity)
    {
        // 验证实体有效性，防止悬空指针
        if (entity && entity->isValid()) {
            _selections = {entity};
        }
        else {
            _selections = {nullptr};
        }
    }

    /**
     * @brief Check if viewport should capture input events
     * @return true if viewport is focused and should receive events
     */
    bool shouldCaptureInput() const { return bViewportFocused; }

    // Get and clear pending viewport resize - called from App before render
    bool getPendingViewportResize(Rect2D &outRect)
    {
        if (_bViewportResizePending) {
            outRect = _pendingViewportRect;
            _bViewportResizePending = false;
            return true;
        }
        return false;
    }

    bool screenToViewport(float screenX, float screenY, float &outX, float &outY) const;
    bool screenToViewport(const glm::vec2 in, glm::vec2 &out) const;

    void onImGuiRender(auto content)
    {
        YA_PROFILE_FUNCTION();
        // ya::DeferredModificationQueue::get().onFrameBegin();

        {
            ya::ImGuiStyleScope style;
            updateWindowFlags(style);

            // Begin main dockspace window
            ImGui::Begin("MainEditor", nullptr, _windowFlags);
        }


        setupDockspace();
        menuBar();
        toolbar();
        viewportWindow();

        _sceneHierarchyPanel.onImGuiRender();
        _detailsView.onImGuiRender();
        _contentBrowserPanel.onImGuiRender();

        // Render file picker dialog
        _filePicker.render();

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
    const ImGuiImageEntry *getOrCreateImGuiTextureID(ya::Ptr<IImageView> imageView, ya::Ptr<Sampler> sampler = nullptr);


  private:
    // UI Methods
    void updateWindowFlags(ya::ImGuiStyleScope &style);
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
    void pickEntity(float viewportX, float viewportY);
    void focusCameraOnEntity(Entity *entity);

  public:
    // Public getters
    glm::vec2                    getViewportSize() const { return _viewportSize; }
    bool                         isViewportFocused() const { return bViewportFocused; }
    bool                         isViewportHovered() const { return bViewportHovered; }
    bool                         isGizmoActive() const; // Check if ImGuizmo is being used or hovered
    bool                         isRightMouseDragging() const { return _bRightMouseDragging; }
    const std::vector<Entity *> &getSelections() const { return _selections; }
    // void      setViewportImage(stdptr<IImageView> image) { _viewportImage = getOrCreateImGuiTextureID(image); }
};

} // namespace ya
