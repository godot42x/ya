#pragma once

#include "Core/Common/FWD.h"

#include "Editor/Panels/AssetInspectorPanel.h"
#include "Editor/Panels/ContentBrowserPanel.h"
#include "Editor/Panels/RuntimeToolsPanel.h"
#include "Core/Base.h"

#include "Core/Camera/Camera.h"

#include "Core/Event.h"
#include "Core/Profiling/Instrumentor.h"
#include "Editor/Inspector/DetailsView.h"
#include "Editor/FilePicker.h"
#include "Editor/ImGui/ImGuiHelper.h"
#include "Runtime/Rendering/Common/RenderOverlay.h"
#include "Render/Core/Image.h"
#include "Render/Core/RenderImage.h"
#include "Runtime/Rendering/Common/RenderViewportSnapshot.h"
#include "Editor/Panels/SceneHierarchyPanel.h"
#include <imgui.h>

#include "Editor/EditorCommon.h"

#include <ImGuizmo.h>
#include <string>
#include <vector>



namespace ya
{

struct App;
struct IImageView;
struct IImage;
struct RenderImage;

using EditorViewportContext      = RenderViewportSnapshot;
using EditorViewportDebugCatalog = RenderViewportDebugCatalog;

struct EditorLayer
{
  private:
    App*                 _app                = nullptr;
    uint64_t             _selectedEntityUUID = 0;
    std::vector<Entity*> _selections;

    // Editor panels
    SceneHierarchyPanel _sceneHierarchyPanel;
    DetailsView         _detailsView;
    ContentBrowserPanel _contentBrowserPanel;
    AssetInspectorPanel _assetInspectorPanel;
    RuntimeToolsPanel   _runtimeToolsPanel;

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
    Rect2D    _viewportMouseRect;
    glm::vec2 _viewportMouseCenter = {0.0f, 0.0f};
    bool      bViewportFocused     = false;
    bool      bViewportHovered     = false;
    bool      _bRightMouseDragging = false; // Track right mouse drag for camera rotation
    glm::vec2 _rightMousePressPos  = {};    // Position when right mouse was pressed
    int       _projectBrowserSelection = -1;
    std::vector<std::string> _discoveredProjects;
    std::string              _projectBrowserError;

    // Editor settings
    glm::vec4 _clearColor = {0.1f, 0.1f, 0.1f, 1.0f};
    float     _debugFloat = 0.0f;
    char      _defaultScenePathBuffer[512] = {};
    bool      _bDefaultScenePathDirty      = false;
    bool      _bShowViewportCameraOverlay  = true;

    // ImGui texture descriptor set cache (editor-only, application layer)
    std::unordered_set<ImGuiImageEntry> _imguiTextureCache; // ImageView -> VkDescriptorSet

    // Gizmo state
    ImGuizmo::OPERATION _gizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      _gizmoMode      = ImGuizmo::LOCAL;

    const ImGuiImageEntry* _playIcon       = nullptr;
    const ImGuiImageEntry* _pauseIcon      = nullptr;
    const ImGuiImageEntry* _stopIcon       = nullptr;
    const ImGuiImageEntry* _simulationIcon = nullptr;
    enum
    {
        Linear = 0,
        Nearest
    } _viewPortSamplerType = Linear;

    uint32_t _resizeTimerHandle = 0;
    Rect2D   _pendingViewportRect; // Pending resize event to be processed in next frame
    bool     _bViewportResizePending = false;

    // Render resources explicitly passed in from App each frame
    EditorViewportContext          _viewportCtx;
    std::shared_ptr<RenderImage>   _viewportDisplayImage = nullptr;
    FreeCamera                     _camera;
    float                          _lastDeltaTime = 0.0f;

    // Per-slot state for the deferred GBuffer debug viewer (RGBA toggle mask + cached swizzled view)
    struct ImageSlotState
    {
        std::string                 configKey;
        std::array<bool, 4>         channelEnabled = {true, true, true, true};
        std::shared_ptr<IImageView> maskedView;
        IImageView*                 lastBase = nullptr;
    };
    std::vector<ImageSlotState> _debugImageSlotStates;

    struct DebugGroupState
    {
        std::string      configKey;
        int              selectedGroupIndex = 0;
        std::vector<int> selectedSlots;
    };
    std::vector<DebugGroupState> _debugGroupStates;

  public:
    Delegate<void(Rect2D /*rect*/)> onViewportResized;

    // File picker for save/load dialogs and asset selection
    FilePicker  _filePicker;
    std::string _currentScenePath; // Current scene file path
    Scene*      _editableScene = nullptr;

  public:
    EditorLayer(App* app);
    ~EditorLayer() = default;

    void onAttach();
    void onDetach();

    /// Open the Asset Inspector for the given relative path
    void inspectAsset(const std::string& relativePath) { _assetInspectorPanel.inspectTexture(relativePath); }

    // Set viewport render context before ImGui render - called from App each frame
    void setViewportContext(const EditorViewportContext& ctx) { _viewportCtx = ctx; }
    void setViewportDisplayImage(std::shared_ptr<RenderImage> image) { _viewportDisplayImage = std::move(image); }
    [[nodiscard]] FreeCamera& getCamera() { return _camera; }
    [[nodiscard]] const FreeCamera& getCamera() const { return _camera; }
    [[nodiscard]] std::vector<RenderOverlayText2D> buildViewportCameraOverlayTexts() const;

    void onUpdate(float dt);
    void setCameraController(FreeCameraController* controller) { _runtimeToolsPanel.setCameraController(controller); }
    void setEditableScene(Scene* scene) { _editableScene = scene; }
    void setCurrentScenePath(std::string scenePath) { _currentScenePath = std::move(scenePath); }
    [[nodiscard]] const std::string& getCurrentScenePath() const { return _currentScenePath; }
    void setSceneContext(Scene* scene)
    {
        _sceneHierarchyPanel.setContext(scene);
    }
    void selectEntity(Entity* entity)
    {
        _sceneHierarchyPanel.setSelection(entity);
    }

    // Entity selection bus - notifies DetailsView of selection changes
    void setSelectedEntity(Entity* entity)
    {
        if (entity && entity->isValid()) {
            _selections = {entity};
            if (auto* idComponent = entity->getComponent<IDComponent>()) {
                _selectedEntityUUID = idComponent->_id.value;
            }
            else {
                _selectedEntityUUID = 0;
            }
        }
        else {
            _selections.clear();
            _selectedEntityUUID = 0;
        }
    }

    /**
     * @brief Check if viewport should capture input events
     * @return true if viewport is focused and should receive events
     */
    bool shouldCaptureInput() const { return bViewportFocused; }
    bool shouldShowViewportCameraOverlay() const { return _bShowViewportCameraOverlay; }

    // Get and clear pending viewport resize - called from App before render
    bool getPendingViewportResize(Rect2D& outRect)
    {
        if (_bViewportResizePending) {
            outRect                 = _pendingViewportRect;
            _bViewportResizePending = false;
            return true;
        }
        return false;
    }

    bool screenToViewport(float screenX, float screenY, float& outX, float& outY) const;
    bool screenToViewport(const glm::vec2 in, glm::vec2& out) const;
    void queueViewportResize(Rect2D rect)
    {
        _pendingViewportRect    = rect;
        _bViewportResizePending = true;
    }

    void onImGuiRender()
    {
        YA_PROFILE_FUNCTION();
        // ya::DeferredModificationQueue::get().onFrameBegin();

        if (!hasProjectLoaded()) {
            projectBrowserWindow();
            return;
        }

        {
            ya::ImGuiStyleScope style;
            updateWindowFlags(style);

            // Begin main dockspace window
            ImGui::Begin("MainEditor", nullptr, _windowFlags);
        }


        setupDockspace();
        menuBar();
        toolbar();
        editorSettings();
        viewportWindow();
        debugWindow();

        _sceneHierarchyPanel.onImGuiRender();
        _detailsView.onImGuiRender();
        _contentBrowserPanel.onImGuiRender();
        _assetInspectorPanel.onImGuiRender();
        statsWindow();
        runtimeToolsWindow();
        renderAuxiliaryUi();

        ImGui::End(); // End main dockspace window
    }
    void onEvent(const Event& event);


    /**
     * @brief Get or create ImGui texture ID for rendering in ImGui::Image()
     * @param imageView Platform image view handle (e.g., VkImageView)
     * @param sampler Platform sampler handle (e.g., VkSampler)
     * @return ImTextureID (VkDescriptorSet as void*)
     */
    const ImGuiImageEntry* getOrCreateImGuiTextureID(ya::Ptr<IImageView> imageView, ya::Ptr<Sampler> sampler = nullptr);
    const ImGuiImageEntry* getOrCreateImGuiDescriptorSet(ya::Ptr<IImageView> imageView, ya::Ptr<Sampler> sampler = nullptr)
    {
        return getOrCreateImGuiTextureID(imageView, sampler);
    }


  private:
    Scene* getEditableScene() const;
    Scene* getViewportInteractionScene() const;
    void   syncEditorSettingsFromConfig();
    [[nodiscard]] bool hasProjectLoaded() const;
    void refreshProjectBrowser();
    [[nodiscard]] bool openProjectInPlace(const std::string& projectPath);

    // UI Methods
    void updateWindowFlags(ya::ImGuiStyleScope& style);
    void menuBar();
    void toolbar();
    void projectBrowserWindow();
    void runtimeToolsWindow();
    void statsWindow();
    void renderAuxiliaryUi();
    // void settingsWindow();
    // void renderStatsWindow();
    void viewportWindow();
    void editorSettings();

    // --
    void debugWindow();
    bool renderDebugImageGroup(const EditorViewportDebugCatalog::Group& group,
                               int                                           groupIndex,
                               const ImVec2&                                 panelSize,
                               bool                                          bUseCollapsingHeader = true,
                               float                                         maxPreviewSize       = 0.0f);
    void renderDebugImageGroups(const ImVec2& panelSize, int categoryFilter = -1);
    void renderDebugImageGroupsGrid(const ImVec2& panelSize, int categoryFilter, float maxPreviewSize = 0.0f);
    void renderDebugImageSlots(const ImVec2& panelSize, int categoryFilter = -1);
    void syncDebugSlotState(const EditorViewportDebugCatalog::Slot& slot, ImageSlotState& state);
    bool renderDebugSlotMaskControls(const EditorViewportDebugCatalog::Slot& slot, ImageSlotState& state);
    void updateDebugSlotImageView(uint32_t slotIndex, const EditorViewportDebugCatalog::Slot& slot, ImageSlotState& state, bool bForceRefresh = false);
    void renderDebugSlotImage(uint32_t slotIndex, const EditorViewportDebugCatalog::Slot& slot, ImageSlotState& state, float width, float height, Sampler* sampler);
    [[nodiscard]] const EditorViewportDebugCatalog& getDebugCatalog() const;
    [[nodiscard]] const RenderViewportDebugImageSlot* getDebugSlotFrame(uint32_t slotIndex) const;

    // Helpers
    void setupDockspace();


    void cleanupImGuiTextures();
    void removeImGuiTexture(const ImGuiImageEntry* entry);
    void renderGizmo();
    void pickEntity(float viewportX, float viewportY);
    void focusCameraOnEntity(Entity* entity);

  public:
    // Public getters
    glm::vec2                   getViewportSize() const { return _viewportSize; }
    bool                        isViewportFocused() const { return bViewportFocused; }
    bool                        isViewportHovered() const { return bViewportHovered; }
    const Rect2D&               getViewportMouseRect() const { return _viewportMouseRect; }
    const glm::vec2&            getViewportMouseCenter() const { return _viewportMouseCenter; }
    bool                        isGizmoActive() const; // Check if ImGuizmo is being used or hovered
    bool                        isRightMouseDragging() const { return _bRightMouseDragging; }
    const std::vector<Entity*>& getSelections() const { return _selections; }
    Entity*                     getSelectedEntity() const { return _sceneHierarchyPanel.getSelectedEntity(); }
    uint64_t                    getSelectedEntityUUID() const { return _selectedEntityUUID; }
    // void      setViewportImage(stdptr<IImageView> image) { _viewportImage = getOrCreateImGuiTextureID(image); }
};

} // namespace ya

