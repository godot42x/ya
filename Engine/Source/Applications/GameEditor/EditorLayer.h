#pragma once

#include "Core/Common/FWD.h"

#include "GameEditor/Panels/AssetInspectorPanel.h"
#include "GameEditor/Panels/ContentBrowserPanel.h"
#include "GameEditor/Panels/RuntimeToolsPanel.h"
#include "GameEditor/Panels/GUIWorkbenchPanel.h"
#include "Core/Base.h"

#include "Core/Camera/Camera.h"

#include "Core/Event.h"
#include "Core/Profiling/Instrumentor.h"
#include "GameEditor/Inspector/DetailsView.h"
#include "GameEditor/FilePicker.h"
#include "GameEditor/ImGui/ImGuiHelper.h"
#include "Render3D/Common/RenderOverlay.h"
#include "RHI/Core/Image.h"
#include "RHI/Core/RenderTexture.h"
#include "Render3D/Common/RenderViewportSnapshot.h"
#include "GameEditor/Panels/SceneHierarchyPanel.h"
#include "GameEditor/Panels/UIDesignerPanel.h"
#include <imgui.h>

#include "GameEditor/EditorCommon.h"

#include <ImGuizmo.h>
#include <algorithm>
#include <string>
#include <vector>



namespace ya
{

struct App;
struct IImageView;
struct IImage;
struct RenderTexture;
using EditorViewportContext      = RenderViewportSnapshot;
using EditorViewportDebugCatalog = RenderViewportDebugCatalog;

/// Editor viewport viewing mode (development-time only; the game always
/// renders 3D + 2D together). Mode2D previews the Game UI designer canvas
/// over a grid, hiding the 3D world.
enum class EViewportMode : uint8_t
{
    Mode3D = 0,
    Mode2D = 1,
};

struct EditorLayer
{
    friend class  EditorViewportCompositor;

  private:
    App*                 _app                = nullptr;
    uint64_t             _selectedEntityUUID = 0;
    std::vector<Entity*> _selections;
    std::string          _selectedWidgetEntryId;    // Mutually exclusive with the above

    // Editor panels
    SceneHierarchyPanel _sceneHierarchyPanel;
    DetailsView         _detailsView;
    ContentBrowserPanel _contentBrowserPanel;
    AssetInspectorPanel _assetInspectorPanel;
    RuntimeToolsPanel   _runtimeToolsPanel;
    UIDesignerPanel     _uiDesignerPanel;
    GUIWorkbenchPanel   _guiWorkbenchPanel;

    // ImGui Docking state
    ImGuiDockNodeFlags _dockspaceFlags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags   _windowFlags    = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    // Window state
    bool bFullscreen     = true;
    bool bPadding        = true;
    bool bShowDemoWindow = false;
    bool bShowRenderGraphWindow = false;
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

    // 2D canvas preview state (Mode2D): pan in viewport pixels, zoom scale
    // around the viewport center. Lightweight navigation state - no camera
    // entity (Unity Scene-view 2D mode semantics).
    EViewportMode _viewportMode = EViewportMode::Mode3D;
    glm::vec2     _canvasPan    = {0.0f, 0.0f};
    float         _canvasZoom   = 1.0f;
    bool          _bCanvasPanning = false;
    glm::vec2     _canvasPanLastMouse = {0.0f, 0.0f};

    // 2D canvas widget direct manipulation (designer preview). The drag
    // session itself (snapshots + delta application) lives in the UI
    // Designer panel; this layer owns the mouse mapping and handle hit test.
    UIElement* _canvasPressHit   = nullptr;       // widget the press hit (drag target)
    glm::vec2  _canvasPressPoint = {0.0f, 0.0f};  // canvas logical point at press
    bool       _bCanvasPressActive = false;       // press handled selection/drag this gesture

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
    std::shared_ptr<RenderTexture> _viewportDisplayImage = nullptr;
    std::shared_ptr<RenderTexture> _entityIdPickImage    = nullptr;
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
    void setViewportDisplayImage(std::shared_ptr<RenderTexture> image) { _viewportDisplayImage = std::move(image); }
    void setEntityIdPickImage(std::shared_ptr<RenderTexture> image) { _entityIdPickImage = std::move(image); }
    [[nodiscard]] const std::shared_ptr<RenderTexture>& getEntityIdPickImage() const { return _entityIdPickImage; }
    [[nodiscard]] FreeCamera& getCamera() { return _camera; }
    [[nodiscard]] const FreeCamera& getCamera() const { return _camera; }
    [[nodiscard]] std::vector<RenderOverlayText2D> buildViewportCameraOverlayTexts() const;

    void onUpdate(float dt);
    void setCameraController(FreeCameraController* controller) { _runtimeToolsPanel.setCameraController(controller); }
    void setEditableScene(Scene* scene);
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

    /// Select a SceneWidgetEntry (clears entity selection).
    void setSelectedWidgetEntryId(const std::string& entryId)
    {
        _selectedWidgetEntryId = entryId;
        if (!entryId.empty()) {
            _selections.clear();
            _selectedEntityUUID = 0;
        }
    }
    [[nodiscard]] const std::string& getSelectedWidgetEntryId() const { return _selectedWidgetEntryId; }
    /// The selected SceneWidgetEntry (nullptr when none/not found).
    SceneWidgetEntry* getSelectedWidgetEntry();

    // Entity selection bus - notifies DetailsView of selection changes
    void setSelectedEntity(Entity* entity)
    {
        setSelections(entity && entity->isValid() ? std::vector<Entity*>{entity} : std::vector<Entity*>{},
                      entity);
    }

    /// Multi-select bus. `primary` is guaranteed to end up at `_selections[0]`
    /// so existing single-selection consumers (gizmo, details, focus) keep working.
    void setSelections(const std::vector<Entity*>& selections, Entity* primary = nullptr)
    {
        _selectedWidgetEntryId.clear();
        _selections.clear();
        for (Entity* entity : selections) {
            if (entity && entity->isValid() &&
                std::find(_selections.begin(), _selections.end(), entity) == _selections.end()) {
                _selections.push_back(entity);
            }
        }
        if (primary && primary->isValid()) {
            auto it = std::find(_selections.begin(), _selections.end(), primary);
            if (it != _selections.end()) {
                _selections.erase(it);
            }
            _selections.insert(_selections.begin(), primary);
        }

        _selectedEntityUUID = 0;
        if (!_selections.empty()) {
            if (auto* idComponent = _selections.front()->getComponent<IDComponent>()) {
                _selectedEntityUUID = idComponent->_id.value;
            }
        }

        if (!_selections.empty() && isViewportMode2D()) {
            setViewportMode(EViewportMode::Mode3D, /*bPersist=*/false);
        }
    }

    // === 2D canvas preview mode ===
    [[nodiscard]] EViewportMode getViewportMode() const { return _viewportMode; }
    void setViewportMode(EViewportMode mode, bool bPersist = true);
    [[nodiscard]] bool isViewportMode2D() const { return _viewportMode == EViewportMode::Mode2D; }
    [[nodiscard]] const glm::vec2& getCanvasPan() const { return _canvasPan; }
    [[nodiscard]] float            getCanvasZoom() const { return _canvasZoom; }
    void setCanvasPan(const glm::vec2& pan) { _canvasPan = pan; }
    void setCanvasZoom(float zoom) { _canvasZoom = std::clamp(zoom, 0.1f, 16.0f); }
    /// Map a viewport-local pixel to canvas logical pixels under the current
    /// 2D pan/zoom transform. Returns false when the point is outside the
    /// visible canvas region.
    bool viewportToCanvas(const glm::vec2& viewportLocal, glm::vec2& outCanvas) const;
    /// Inverse of viewportToCanvas (viewport-local px from canvas logical px).
    [[nodiscard]] glm::vec2 canvasToViewport(const glm::vec2& canvasPoint) const;

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
        _uiDesignerPanel.onImGuiRender();
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
    Scene* getSceneHierarchyContext() const;
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
    void renderGraphWindow();
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
    /// 2D mode picking: hit-test the UI Designer preview tree (canvas coords).
    void pickNode2D(float viewportX, float viewportY);

    // === 2D canvas direct manipulation (designer preview) ===
    /// Left-press in the 2D canvas: resize handle of the selection takes
    /// priority, then hit the preview tree (select + start move), then clear
    /// the selection on empty canvas.
    void beginCanvasPress();
    /// Left-drag while a manipulation session is active.
    void updateCanvasDrag();
    /// Left-release: end the manipulation session (no pick when a drag ran).
    void endCanvasPress();
    /// Resize-handle hit test of `widget` in viewport-local mouse pixels
    /// (0 when the cursor is not over a handle).
    uint8_t hitTestCanvasResizeHandles(const UIElement& widget) const;
    /// Frame the camera on the merged world bounds of the whole selection.
    void focusCameraOnSelection();

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
    [[nodiscard]] UIDesignerPanel& getUIDesignerPanel() { return _uiDesignerPanel; }
    [[nodiscard]] GUIWorkbenchPanel& getGUIWorkbenchPanel() { return _guiWorkbenchPanel; }
    Entity*                     getSelectedEntity() const { return _selections.empty() ? nullptr : _selections.front(); }
    uint64_t                    getSelectedEntityUUID() const { return _selectedEntityUUID; }
    /// Active scene used for viewport interaction. In the 2D workspace this is
    /// always the authoring scene so runtime UI editing never mutates the play
    /// clone. In the 3D workspace it follows the active scene.
    Scene* getViewportInteractionScene() const;
    // void      setViewportImage(stdptr<IImageView> image) { _viewportImage = getOrCreateImGuiTextureID(image); }
};

} // namespace ya
