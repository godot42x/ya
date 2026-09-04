#pragma once

#include "Core/Common/FWD.h"

#include "Core/Base.h"
#include "GameEditor/Panels/AssetInspectorPanel.h"
#include "GameEditor/Panels/ContentBrowserPanel.h"

#include "Core/Camera/Camera.h"

#include "Core/Event.h"
#include "Core/Profiling/Instrumentor.h"
#include "GameEditor/FilePicker.h"
#include "GameEditor/UI/EditorAssetPicker.h"
#include "GameEditor/UI/EditorFilePicker.h"
#include "GameEditor/UI/EditorViewportHost.h"
#include "GameEditor/ImGui/ImGuiHelper.h"
#include "GameEditor/Panels/SceneHierarchyPanel.h"
#include "GameEditor/Panels/UIDesignerPanel.h"
#include "GameEditor/UI/EditorTransformUndo.h"
#include "RHI/Core/Image.h"
#include "RHI/Core/RenderTexture.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/RenderViewportSnapshot.h"
#include <imgui.h>

#include "GameEditor/EditorCommon.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>



namespace ya
{

struct App;
struct IImageView;
struct IImage;
struct RenderTexture;
struct Texture;
class UndoStack;
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

enum class EEditorViewportGizmoOperation : uint8_t
{
    Translate = 0,
    Rotate    = 1,
    Scale     = 2,
};

enum class EEditorViewportGizmoMode : uint8_t
{
    Local = 0,
    World = 1,
};

enum class EEditorViewportGizmoAxis : uint8_t
{
    None = 0,
    X    = 1,
    Y    = 2,
    Z    = 3,
};

struct EditorLayer
{
    friend class EditorViewportCompositor;
    friend class EditorViewportGizmoOverlay;

  private:
    App*                 _app                = nullptr;
    uint64_t             _selectedEntityUUID = 0;
    uint64_t             _selectionGeneration = 0;
    std::vector<Entity*> _selections;
    std::string          _selectedWidgetEntryId; // Mutually exclusive with the above

    // Editor panels
    SceneHierarchyPanel _sceneHierarchyPanel;
    ContentBrowserPanel _contentBrowserPanel;
    AssetInspectorPanel _assetInspectorPanel;
    UIDesignerPanel     _uiDesignerPanel;

    // Viewport state
    glm::vec2                _viewportSize = {1280.f, 720.f};
    glm::vec2                _viewportBounds[2]; // Min and max bounds
    Rect2D                   viewportRect;
    Rect2D                   _viewportMouseRect;
    glm::vec2                _viewportMouseCenter     = {0.0f, 0.0f};
    bool                     bViewportFocused         = false;
    bool                     bViewportHovered         = false;
    bool                     _bRightMouseDragging     = false; // Track right mouse drag for camera rotation
    glm::vec2                _rightMousePressPos      = {};    // Position when right mouse was pressed
    int                      _projectBrowserSelection = -1;
    std::vector<std::string> _discoveredProjects;
    std::string              _projectBrowserError;

    // 2D canvas preview state (Mode2D): pan in viewport pixels, zoom scale
    // around the viewport center. Lightweight navigation state - no camera
    // entity (Unity Scene-view 2D mode semantics).
    EViewportMode _viewportMode       = EViewportMode::Mode3D;
    glm::vec2     _canvasPan          = {0.0f, 0.0f};
    float         _canvasZoom         = 1.0f;
    bool          _bCanvasPanning     = false;
    glm::vec2     _canvasPanLastMouse = {0.0f, 0.0f};

    // 2D canvas widget direct manipulation (designer preview). The drag
    // session itself (snapshots + delta application) lives in the UI
    // Designer panel; this layer owns the mouse mapping and handle hit test.
    UIElement* _canvasPressHit     = nullptr;      // widget the press hit (drag target)
    glm::vec2  _canvasPressPoint   = {0.0f, 0.0f}; // canvas logical point at press
    bool       _bCanvasPressActive = false;        // press handled selection/drag this gesture

    // Editor settings
    glm::vec4 _clearColor                  = {0.1f, 0.1f, 0.1f, 1.0f};
    float     _debugFloat                  = 0.0f;
    char      _defaultScenePathBuffer[512] = {};
    bool      _bDefaultScenePathDirty      = false;
    bool      _bShowViewportCameraOverlay  = true;

    // ImGui texture descriptor set cache (editor-only, application layer)
    std::unordered_set<ImGuiImageEntry> _imguiTextureCache; // ImageView -> VkDescriptorSet

    // Viewport gizmo state
    EEditorViewportGizmoOperation _gizmoOperation = EEditorViewportGizmoOperation::Translate;
    EEditorViewportGizmoMode      _gizmoMode      = EEditorViewportGizmoMode::Local;
    UndoStack*                    _gizmoUndo      = nullptr;
    FEditorViewportHostState      _viewportGizmoHostState{};
    bool                          _bViewportGizmoHostValid = false;
    bool                          _bViewportGizmoHovered = false;
    bool                          _bViewportGizmoDragging = false;
    bool                          _bViewportGizmoPointerInside = false;
    bool                          _bViewportGizmoConsumeReleasePick = false;
    glm::vec2                     _viewportGizmoPointerLocal = {0.0f, 0.0f};
    EEditorViewportGizmoAxis      _gizmoHoveredAxis = EEditorViewportGizmoAxis::None;
    EEditorViewportGizmoAxis      _gizmoActiveAxis  = EEditorViewportGizmoAxis::None;
    glm::mat4                     _gizmoDragStartPrimaryWorld = glm::mat4(1.0f);
    glm::vec3                     _gizmoDragAxisWorld = {0.0f, 0.0f, 0.0f};
    glm::vec3                     _gizmoDragOriginWorld = {0.0f, 0.0f, 0.0f};
    glm::vec3                     _gizmoDragPlaneNormal = {0.0f, 0.0f, 0.0f};
    glm::vec3                     _gizmoDragStartPlaneVector = {0.0f, 0.0f, 0.0f};
    float                         _gizmoDragStartScalar = 0.0f;
    std::vector<FEditorTransformSnapshot> _gizmoUndoBefore;

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
        std::shared_ptr<IImageView> identityView;
        std::shared_ptr<Texture>    previewTexture;
        IImageView*                 lastBase     = nullptr;
        IImageView*                 previewView  = nullptr;
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
    std::function<void()> _saveSceneAsHandler;
    EditorAssetPickerCallback _assetPickerHandler;
    EditorFilePickerCallback  _filePickerHandler;
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
    void                                                setViewportContext(const EditorViewportContext& ctx) { _viewportCtx = ctx; }
    void                                                setViewportDisplayImage(std::shared_ptr<RenderTexture> image) { _viewportDisplayImage = std::move(image); }
    void                                                setEntityIdPickImage(std::shared_ptr<RenderTexture> image) { _entityIdPickImage = std::move(image); }
    [[nodiscard]] const std::shared_ptr<RenderTexture>& getEntityIdPickImage() const { return _entityIdPickImage; }
    [[nodiscard]] FreeCamera&                           getCamera() { return _camera; }
    [[nodiscard]] const FreeCamera&                     getCamera() const { return _camera; }
    [[nodiscard]] std::vector<RenderOverlayText2D>      buildViewportCameraOverlayTexts() const;

    void                                                onUpdate(float dt);
    void                                                setEditableScene(Scene* scene);
    void                                                setCurrentScenePath(std::string scenePath) { _currentScenePath = std::move(scenePath); }
    [[nodiscard]] const std::string&                    getCurrentScenePath() const { return _currentScenePath; }
    [[nodiscard]] bool                                  isProjectLoaded() const { return hasProjectLoaded(); }
    [[nodiscard]] const std::shared_ptr<RenderTexture>& getViewportDisplayImage() const
    {
        return _viewportDisplayImage;
    }
    [[nodiscard]] const EditorViewportDebugCatalog& getDebugCatalog() const;
    [[nodiscard]] const RenderViewportDebugImageSlot* getDebugSlotFrame(uint32_t slotIndex) const;
    [[nodiscard]] std::array<bool, 4> getDebugChannelMask(uint32_t slotIndex);
    void setDebugChannelMask(uint32_t slotIndex, std::array<bool, 4> mask);
    [[nodiscard]] int getDebugGroupSelectedIndex(int groupIndex);
    void setDebugGroupSelectedIndex(int groupIndex, int selectedIndex);
    [[nodiscard]] int getDebugGroupItemSlot(int groupIndex, uint32_t itemIndex);
    void setDebugGroupItemSlot(int groupIndex, uint32_t itemIndex, int selectedSlot);
    [[nodiscard]] std::shared_ptr<Texture> getDebugSlotPreviewTexture(uint32_t slotIndex);
    void                                          notifyViewportWidgetRect(const Rect2D& rect);
    void                                          setViewportHoverFocus(bool hovered, bool focused);
    [[nodiscard]] const std::vector<std::string>& getDiscoveredProjects() const
    {
        return _discoveredProjects;
    }
    [[nodiscard]] int                getProjectBrowserSelection() const { return _projectBrowserSelection; }
    void                             setProjectBrowserSelection(int index) { _projectBrowserSelection = index; }
    [[nodiscard]] const std::string& getProjectBrowserError() const { return _projectBrowserError; }
    void                             requestRefreshProjectBrowser() { refreshProjectBrowser(); }
    bool                             requestOpenProject(const std::string& projectPath) { return openProjectInPlace(projectPath); }
    [[nodiscard]] Scene*             getHierarchyScene() const { return getSceneHierarchyContext(); }
    void                             setSceneContext(Scene* scene)
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
        cancelViewportGizmoDrag();
        _selectedWidgetEntryId = entryId;
        if (!entryId.empty()) {
            _selections.clear();
            _selectedEntityUUID = 0;
        }
        ++_selectionGeneration;
    }
    [[nodiscard]] const std::string& getSelectedWidgetEntryId() const { return _selectedWidgetEntryId; }
    /// The selected SceneWidgetEntry (nullptr when none/not found).
    SceneWidgetEntry* getSelectedWidgetEntry();

    // Entity selection bus - notifies inspector consumers of selection changes
    void setSelectedEntity(Entity* entity)
    {
        setSelections(entity && entity->isValid() ? std::vector<Entity*>{entity} : std::vector<Entity*>{},
                      entity);
    }

    /// Multi-select bus. `primary` is guaranteed to end up at `_selections[0]`
    /// so existing single-selection consumers (gizmo, details, focus) keep working.
    void setSelections(const std::vector<Entity*>& selections, Entity* primary = nullptr)
    {
        cancelViewportGizmoDrag();
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
        ++_selectionGeneration;
    }

    [[nodiscard]] uint64_t selectionGeneration() const { return _selectionGeneration; }

    // === 2D canvas preview mode ===
    [[nodiscard]] EViewportMode    getViewportMode() const { return _viewportMode; }
    void                           setViewportMode(EViewportMode mode, bool bPersist = true);
    [[nodiscard]] bool             isViewportMode2D() const { return _viewportMode == EViewportMode::Mode2D; }
    [[nodiscard]] const glm::vec2& getCanvasPan() const { return _canvasPan; }
    [[nodiscard]] float            getCanvasZoom() const { return _canvasZoom; }
    void                           setCanvasPan(const glm::vec2& pan) { _canvasPan = pan; }
    void                           setCanvasZoom(float zoom) { _canvasZoom = std::clamp(zoom, 0.1f, 16.0f); }
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
    void setShowViewportCameraOverlay(bool enabled);
    [[nodiscard]] int getViewportSamplerType() const { return static_cast<int>(_viewPortSamplerType); }
    void setViewportSamplerType(int samplerType);
    [[nodiscard]] std::string getDefaultScenePathDraft() const { return _defaultScenePathBuffer; }
    void setDefaultScenePathDraft(std::string path);
    [[nodiscard]] bool isDefaultScenePathDirty() const { return _bDefaultScenePathDirty; }
    void applyDefaultScenePathDraft();
    void resetDefaultScenePathDraft();
    [[nodiscard]] bool defaultScenePathExists() const;

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

  public:
    Scene* getEditableScene() const;

  private:
    Scene*             getSceneHierarchyContext() const;
    void               syncEditorSettingsFromConfig();
    [[nodiscard]] bool hasProjectLoaded() const;
    void               refreshProjectBrowser();
    [[nodiscard]] bool openProjectInPlace(const std::string& projectPath);

    void                                              syncDebugSlotState(const EditorViewportDebugCatalog::Slot& slot, ImageSlotState& state);
    void                                              updateDebugSlotImageView(uint32_t slotIndex, const EditorViewportDebugCatalog::Slot& slot, ImageSlotState& state, bool bForceRefresh = false);
    void                                              ensureDebugViewerState();
    void                                              loadDebugGroupState(int groupIndex);
    void                                              persistDebugGroupState(int groupIndex);

    void cleanupImGuiTextures();
    void removeImGuiTexture(const ImGuiImageEntry* entry);
    [[nodiscard]] bool hasViewportGizmoSelection() const;
    void syncViewportGizmoHost(const FEditorViewportHostState& host);
    void setViewportGizmoPointer(const glm::vec2& localPoint, bool insideViewport);
    [[nodiscard]] bool beginViewportGizmoDrag(const glm::vec2& localPoint);
    void updateViewportGizmoDrag(const glm::vec2& localPoint);
    void endViewportGizmoDrag();
    void cancelViewportGizmoDrag();
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
    glm::vec2                        getViewportSize() const { return _viewportSize; }
    bool                             isViewportFocused() const { return bViewportFocused; }
    bool                             isViewportHovered() const { return bViewportHovered; }
    const Rect2D&                    getViewportMouseRect() const { return _viewportMouseRect; }
    const glm::vec2&                 getViewportMouseCenter() const { return _viewportMouseCenter; }
    bool                             isGizmoActive() const;
    bool                             isViewportGizmoDragging() const { return _bViewportGizmoDragging; }
    void                             recordViewportGizmoOverlay() const;
    void                             setViewportGizmoOperation(EEditorViewportGizmoOperation operation);
    void                             setViewportGizmoUndoStack(UndoStack* undo) { _gizmoUndo = undo; }
    bool                             isRightMouseDragging() const { return _bRightMouseDragging; }
    const std::vector<Entity*>&      getSelections() const { return _selections; }
    [[nodiscard]] UIDesignerPanel&   getUIDesignerPanel() { return _uiDesignerPanel; }
    [[nodiscard]] AssetInspectorPanel& getAssetInspectorPanel() { return _assetInspectorPanel; }

    Entity*  getSelectedEntity() const { return _selections.empty() ? nullptr : _selections.front(); }
    uint64_t getSelectedEntityUUID() const { return _selectedEntityUUID; }
    /// Active scene used for viewport interaction. In the 2D workspace this is
    /// always the authoring scene so runtime UI editing never mutates the play
    /// clone. In the 3D workspace it follows the active scene.
    Scene* getViewportInteractionScene() const;
    // void      setViewportImage(stdptr<IImageView> image) { _viewportImage = getOrCreateImGuiTextureID(image); }

    void cmdNewScene();
    void cmdSaveScene();
    void cmdSaveSceneAs();
    [[nodiscard]] bool canViewportAuthor() const;
    void cmdCreateEmptyNode();
    void cmdCreateNodePreset(const std::string& presetDisplayName);
    void cmdDuplicateSelection();
    void cmdDeleteSelection();
    void setSaveSceneAsHandler(std::function<void()> handler) { _saveSceneAsHandler = std::move(handler); }
    void clearSaveSceneAsHandler() { _saveSceneAsHandler = nullptr; }
    void setAssetPickerHandler(EditorAssetPickerCallback handler) { _assetPickerHandler = std::move(handler); }
    void clearAssetPickerHandler() { _assetPickerHandler = nullptr; }
    void setFilePickerHandler(EditorFilePickerCallback handler) { _filePickerHandler = std::move(handler); }
    void clearFilePickerHandler() { _filePickerHandler = nullptr; }
};

} // namespace ya
