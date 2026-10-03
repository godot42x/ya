#pragma once

#include "Core/Common/FWD.h"

#include "Core/Base.h"

#include "Core/Camera/Camera.h"

#include "Core/Event.h"
#include "Core/Profiling/Instrumentor.h"
#include "GameEditor/FilePicker.h"
#include "GameEditor/UI/Dialogs/EditorAssetPicker.h"
#include "GameEditor/UI/Dialogs/EditorFilePicker.h"
#include "GameEditor/UI/Viewport/EditorViewportGizmoController.h"
#include "GameEditor/UI/Viewport/EditorTileBrushController.h"
#include "GameEditor/EditorSelection.h"
#include "GameEditor/EditorUIDesignerSession.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "RHI/Core/Image.h"
#include "RHI/Core/RenderTexture.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/RenderViewportSnapshot.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <functional>
#include <string>
#include <vector>



namespace ya
{

struct App;
struct Node;
struct EditorDocumentRegistry;
struct UIDocumentStore;
struct IImageView;
struct IImage;
struct RenderTexture;
struct Texture;
using EditorViewportContext      = RenderViewportSnapshot;
using EditorViewportDebugCatalog = RenderViewportDebugCatalog;

struct EditorLayer
{
  private:
    App*                 _app                = nullptr;
    uint64_t             _selectedEntityUUID = 0;
    uint64_t             _selectionGeneration = 0;
    std::vector<Entity*> _selections;
    std::string          _selectedWidgetEntryId; // Mutually exclusive with the above

    // Domain models (not retained UI). WidgetTree views live on EditorSurface tabs.
    EditorSelection         _selection;
    EditorUIDesignerSession _uiDesignerSession;
    /// Content Browser -> Asset Inspector tab. An empty path means nothing is
    /// being inspected.
    std::string _inspectedAssetPath;
    EditorDocumentRegistry* _documents = nullptr;
    UIDocumentStore*        _uiDocumentStore = nullptr;

    /// How many editor viewports are on screen. The viewport is a docked tab, so
    /// the dock detaches its widget whenever another tab in its stack is selected
    /// (or the level editor tab that owns that stack is), and there is then no
    /// viewport to draw into. The editor declares its authoring View only while
    /// this is nonzero: a hidden viewport costs no world graph. A count rather
    /// than a flag because a second editor window has its own chrome and its own
    /// viewport, and one window switching tabs must not stop the other's.
    /// Written only by the window chrome, at the viewport widget's attach/detach
    /// edge.
    uint32_t _shownViewportCount = 0;

    // Authoring viewport geometry (chrome). The editor declares its authoring
    // View with this rect and maps viewport input through it; it is not the
    // present surface. Before the first layout the panel has no rect yet, so
    // the default size stands in (see getViewportRect).
    glm::vec2                _viewportSize = {1280.f, 720.f};
    /// Device pixels per logical point of the window that shows this panel.
    float                    _viewportPixelDensity = 1.0f;
    glm::vec2                _viewportBounds[2]; // Min and max bounds
    Rect2D                   viewportRect;
    /// Camera preview panel stacked on the viewport image, in tree-logical
    /// pixels; empty when no preview is shown. It is chrome, so a point on it
    /// is not a point on the world image and the layer must not map it into
    /// viewport-local world coordinates.
    Rect2D                   _viewportPreviewPanelRect;
    Rect2D                   _viewportMouseRect;
    glm::vec2                _viewportMouseCenter     = {0.0f, 0.0f};
    bool                     bViewportFocused         = false;
    bool                     bViewportHovered         = false;
    bool                     _bRightMouseDragging     = false; // Track right mouse drag for camera rotation
    glm::vec2                _rightMousePressPos      = {};    // Position when right mouse was pressed
    int                      _projectBrowserSelection = -1;
    std::vector<std::string> _discoveredProjects;
    std::string              _projectBrowserError;

    /// World-2D authoring is this viewport with an orthographic camera looking
    /// down local +Z onto the XY plane. The UI Designer canvas is not a mode of
    /// this viewport; it is the UI Designer's own Canvas tab.
    bool          _bEditorOrthoXY     = false;

    // Editor settings
    glm::vec4 _clearColor                  = {0.1f, 0.1f, 0.1f, 1.0f};
    float     _debugFloat                  = 0.0f;
    char      _defaultScenePathBuffer[512] = {};
    bool      _bDefaultScenePathDirty      = false;
    bool      _bShowViewportCameraOverlay  = true;
    /// Editor view option (`View > Show Editor Gizmos`). Generated editor
    /// companions (camera body, light icons) are editor furniture: the
    /// authoring viewport draws them while authoring regardless, and this
    /// requests them outside authoring. One switch by design -- a per-object
    /// flag would scatter gizmo state through scene data.
    bool      _bShowEditorGizmos           = false;

    EditorViewportGizmoController _gizmo;
    EditorTileBrushController   _tileBrush;

    enum
    {
        Linear = 0,
        Nearest
    } _viewPortSamplerType = Linear;

    // Render resources explicitly passed in from App each frame
    EditorViewportContext          _viewportCtx;
    std::shared_ptr<RenderTexture> _viewportDisplayImage = nullptr;
    std::shared_ptr<Texture>       _viewportPreviewImage = nullptr;
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
    MulticastDelegate<void()>       onSelectionChanged;
    MulticastDelegate<void()>       onHierarchyChanged;
    MulticastDelegate<void()>       onScenePathChanged;

    // File picker for save/load dialogs and asset selection
    FilePicker  _filePicker;
    std::function<void()> _saveSceneAsHandler;
    EditorAssetPickerCallback _assetPickerHandler;
    EditorFilePickerCallback  _filePickerHandler;
    std::function<void()>     _showContentBrowser;
    std::function<void()>     _showUIDesignerCanvas;
    std::function<bool(EEditorDocumentKind, std::string)> _openDocumentEditor;
    std::string               _pendingContentReveal;
    std::string _currentScenePath; // Current scene file path
    Scene*      _editableScene = nullptr;
    bool        _bSceneDirty = false;
    std::function<void(std::function<void()>)> _unsavedGuard;

  public:
    EditorLayer(App* app);
    ~EditorLayer() = default;

    void onAttach();
    void onDetach();

    /// Open the Asset Inspector for the given relative path. The path is the
    /// whole state: "nothing inspected" is an empty path, so there is no separate
    /// visible flag to keep in step with it. It lives here because the Content
    /// Browser writes it and the Asset Inspector tab reads it.
    void inspectAsset(const std::string& relativePath) { _inspectedAssetPath = relativePath; }
    [[nodiscard]] const std::string& inspectedAssetPath() const { return _inspectedAssetPath; }

    // Set viewport render context before chrome tick - called from App each frame
    void                                                setViewportContext(const EditorViewportContext& ctx) { _viewportCtx = ctx; }
    void                                                setViewportDisplayImage(std::shared_ptr<RenderTexture> image) { _viewportDisplayImage = std::move(image); }
    /// The camera preview's image, published by the viewport compose step from
    /// the preview View's own output. Null when no preview this tick, which is
    /// what collapses the preview panel.
    void                                                setViewportPreviewImage(std::shared_ptr<Texture> image) { _viewportPreviewImage = std::move(image); }
    [[nodiscard]] const std::shared_ptr<Texture>&       getViewportPreviewImage() const { return _viewportPreviewImage; }
    void                                                setEntityIdPickImage(std::shared_ptr<RenderTexture> image) { _entityIdPickImage = std::move(image); }
    [[nodiscard]] const std::shared_ptr<RenderTexture>& getEntityIdPickImage() const { return _entityIdPickImage; }
    [[nodiscard]] FreeCamera&                           getCamera() { return _camera; }
    [[nodiscard]] const FreeCamera&                     getCamera() const { return _camera; }
    [[nodiscard]] std::vector<RenderOverlayText2D>      buildViewportCameraOverlayTexts() const;

    void                                                onUpdate(float dt);
    void                                                setEditableScene(Scene* scene);
    /// Both tables the designer session reads: the dirty/undo registry and the
    /// Game UI document store (SceneWidgetEntry::documentPath -> live
    /// UIDocument). They are app-owned, so the layer only forwards them.
    void bindDocumentServices(EditorDocumentRegistry* documents, UIDocumentStore* uiDocuments);
    [[nodiscard]] EditorDocumentRegistry*               documentRegistry() const { return _documents; }
    [[nodiscard]] UIDocumentStore*                      uiDocumentStore() const { return _uiDocumentStore; }
    void                                                setCurrentScenePath(std::string scenePath);
    [[nodiscard]] const std::string&                    getCurrentScenePath() const { return _currentScenePath; }
    void                                                markSceneDirty() { _bSceneDirty = true; }
    void                                                clearSceneDirty() { _bSceneDirty = false; }
    [[nodiscard]] bool                                  isSceneDirty() const { return _bSceneDirty; }
    void setUnsavedGuard(std::function<void(std::function<void()>)> handler)
    {
        _unsavedGuard = std::move(handler);
    }
    void clearUnsavedGuard() { _unsavedGuard = nullptr; }
    void runAfterUnsavedResolved(std::function<void()> proceed);
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
    /// Panel geometry of the viewport tab, in tree-logical pixels: the world
    /// image rect and the camera preview panel stacked on it (empty when no
    /// preview is shown). Both are chrome geometry of the same host and arrive
    /// together, so the layer can tell "on the world image" from "on the
    /// preview panel" without holding a second copy of the layout.
    void notifyViewportWidgetRect(const Rect2D& rect, const Rect2D& previewPanelRect);
    /// Device pixels per logical point. The authoring view's render target is
    /// `getViewportRect().extent *` this. 1 until the surface publishes a scale.
    void setViewportPixelDensity(float density)
    {
        _viewportPixelDensity = density > 0.0f ? density : 1.0f;
    }
    [[nodiscard]] float viewportPixelDensity() const { return _viewportPixelDensity; }
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
        _selection.setContext(scene);
        notifyHierarchyChanged();
    }
    void notifyHierarchyChanged() { onHierarchyChanged.broadcast(); }
    void selectEntity(Entity* entity)
    {
        _selection.setSelection(entity);
    }

    /// Select a SceneWidgetEntry (clears entity selection).
    void setSelectedWidgetEntryId(const std::string& entryId)
    {
        _gizmo.cancelDrag();
        _selectedWidgetEntryId = entryId;
        if (!entryId.empty()) {
            _selections.clear();
            _selectedEntityUUID = 0;
        }
        ++_selectionGeneration;
        onSelectionChanged.broadcast();
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
        _gizmo.cancelDrag();
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

        ++_selectionGeneration;
        onSelectionChanged.broadcast();
    }

    [[nodiscard]] uint64_t selectionGeneration() const { return _selectionGeneration; }

    // === World-2D authoring and Game UI mounts ===
    [[nodiscard]] bool             isEditorOrthoXY() const { return _bEditorOrthoXY; }
    /// Snap the editor camera onto the XY plane when enabling. Sprites face
    /// local +Z, so this is the view that shows them.
    void                           setEditorOrthoXY(bool enabled);
    /// Write a new canvas document under Content:UI and mount it on the
    /// editable scene, then open it in the designer.
    void                           createAndMountGameUI();
    /// Mount the document currently open in the designer, if it is not already.
    void                           mountOpenGameUI();
    /// A designer document is open and the editable scene does not mount it yet.
    [[nodiscard]] bool             canMountOpenGameUI() const;
    /// Open this scene UI entry in the designer.
    void                           openGameUIEntry(const std::string& entryId);
    /// Remove the entry from the editable scene. The document stays on disk.
    void                           unmountGameUIEntry(const std::string& entryId);

    // === Editor view options (what the editor's own views draw) ===
    [[nodiscard]] bool isEditorGizmoShown() const { return _bShowEditorGizmos; }
    void               setEditorGizmoShown(bool bShow) { _bShowEditorGizmos = bShow; }

    /// True while the viewport has hover/focus, or while RMB look is held so
    /// the editor camera keeps receiving InputManager state after the pointer
    /// leaves the image.
    [[nodiscard]] bool shouldCaptureInput() const;
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

    /// The editor's authoring panel geometry: what the editor declares as its
    /// authoring View's rect. A declared View rect has to describe whole pixels
    /// (the render graph sizes the View's textures from it), so a panel that has
    /// not been laid out yet -- or one too small to hold a pixel -- falls back to
    /// the editor's own default size instead of declaring a rect that rounds to
    /// nothing.
    [[nodiscard]] Rect2D getViewportRect() const
    {
        if (describesPixels(viewportRect)) {
            return viewportRect;
        }
        return Rect2D{.pos = {0.0f, 0.0f}, .extent = _viewportSize};
    }

    /// Whether any editor viewport is on screen. False means the editor declares
    /// no authoring View at all, so a viewport that is not displayed (another tab
    /// in its stack is selected, or the level editor tab owning that stack is)
    /// costs no world graph. `getViewportRect()` keeps its last-laid-out fallback
    /// for the remaining case: shown, but not laid out yet.
    [[nodiscard]] bool isViewportShown() const { return _shownViewportCount > 0; }
    /// Paired with the viewport widget's attach/detach. Each editor window calls
    /// these once per transition, so a window that is not showing its viewport
    /// never cancels one that is.
    void addViewportShown();
    void removeViewportShown();

    /// True when a rect is finite and at least one whole pixel wide and tall on
    /// both axes. A positive comparison alone is not enough: uninitialized or
    /// sub-pixel geometry passes it and then rounds to a zero-sized View.
    [[nodiscard]] static bool describesPixels(const Rect2D& rect)
    {
        if (!std::isfinite(rect.extent.x) || !std::isfinite(rect.extent.y)) {
            return false;
        }
        const Extent2D pixelExtent = Extent2D::fromVec2(rect.extent);
        return pixelExtent.width > 0 && pixelExtent.height > 0;
    }

    bool screenToViewport(float screenX, float screenY, float& outX, float& outY) const;
    bool screenToViewport(const glm::vec2 in, glm::vec2& out) const;

    void onEvent(const Event& event);

  public:
    Scene* getEditableScene() const;

  private:
    Scene*             getSceneHierarchyContext() const;
    void               syncEditorSettingsFromConfig();
    [[nodiscard]] bool hasProjectLoaded() const;
    void               refreshProjectBrowser();
    [[nodiscard]] bool openProjectInPlace(const std::string& projectPath);
    /// Designer's default canvas size is the project's UI reference resolution
    /// when the project sets one.
    void syncDesignResolutionFromProject();

    void                                              syncDebugSlotState(const EditorViewportDebugCatalog::Slot& slot, ImageSlotState& state);
    void                                              updateDebugSlotImageView(uint32_t slotIndex, const EditorViewportDebugCatalog::Slot& slot, ImageSlotState& state, bool bForceRefresh = false);
    void                                              ensureDebugViewerState();
    void                                              loadDebugGroupState(int groupIndex);
    void                                              persistDebugGroupState(int groupIndex);

    void pickEntity(float viewportX, float viewportY);
    /// Frame the camera on the merged world bounds of the whole selection.
    void focusCameraOnSelection();

  public:
    // Public getters
    glm::vec2                        getViewportSize() const { return _viewportSize; }
    bool                             isViewportFocused() const { return bViewportFocused; }
    bool                             isViewportHovered() const { return bViewportHovered; }
    const Rect2D&                    getViewportMouseRect() const { return _viewportMouseRect; }
    const glm::vec2&                 getViewportMouseCenter() const { return _viewportMouseCenter; }
    [[nodiscard]] EditorViewportGizmoController&       gizmo() { return _gizmo; }
    [[nodiscard]] EditorTileBrushController&         tileBrush() { return _tileBrush; }
    [[nodiscard]] const EditorTileBrushController& tileBrush() const { return _tileBrush; }
    [[nodiscard]] const EditorViewportGizmoController& gizmo() const { return _gizmo; }
    bool                             isRightMouseDragging() const { return _bRightMouseDragging; }
    const std::vector<Entity*>&      getSelections() const { return _selections; }
    [[nodiscard]] EditorUIDesignerSession&   getEditorUIDesignerSession() { return _uiDesignerSession; }

    Entity*  getSelectedEntity() const { return _selections.empty() ? nullptr : _selections.front(); }
    uint64_t getSelectedEntityUUID() const { return _selectedEntityUUID; }
    /// The camera the camera-preview inset shows: the selected entity when it
    /// holds a camera, otherwise nothing.
    [[nodiscard]] Entity* getCameraPreviewEntity() const;
    /// Active scene used for viewport interaction: it follows the active scene,
    /// so during a play session it is the play clone.
    Scene* getViewportInteractionScene() const;

    void cmdNewScene();
    void cmdLoadScene(std::string scenePath);
    void cmdSaveScene();
    void cmdSaveSceneAs();
    void cmdOpenScene();
    void cmdRequestQuit();
    [[nodiscard]] bool canViewportAuthor() const;
    /// `parent` nullptr = scene root.
    void cmdCreateEmptyNode(Node* parent = nullptr);
    void cmdCreateNodePreset(const std::string& presetDisplayName, Node* parent = nullptr);
    void cmdDuplicateSelection();
    void cmdDeleteSelection();
    void setSaveSceneAsHandler(std::function<void()> handler) { _saveSceneAsHandler = std::move(handler); }
    void clearSaveSceneAsHandler() { _saveSceneAsHandler = nullptr; }
    void setAssetPickerHandler(EditorAssetPickerCallback handler) { _assetPickerHandler = std::move(handler); }
    void clearAssetPickerHandler() { _assetPickerHandler = nullptr; }
    void setFilePickerHandler(EditorFilePickerCallback handler) { _filePickerHandler = std::move(handler); }
    void clearFilePickerHandler() { _filePickerHandler = nullptr; }
    void setShowContentBrowserHandler(std::function<void()> handler) { _showContentBrowser = std::move(handler); }
    void clearShowContentBrowserHandler() { _showContentBrowser = nullptr; }
    void setShowUIDesignerCanvasHandler(std::function<void()> handler) { _showUIDesignerCanvas = std::move(handler); }
    void clearShowUIDesignerCanvasHandler() { _showUIDesignerCanvas = nullptr; }
    /// Bring the UI Designer's Canvas tab forward (a document was just opened).
    void showUIDesignerCanvas()
    {
        if (_showUIDesignerCanvas) {
            _showUIDesignerCanvas();
        }
    }
    void setOpenDocumentEditorHandler(std::function<bool(EEditorDocumentKind, std::string)> handler)
    {
        _openDocumentEditor = std::move(handler);
    }
    void clearOpenDocumentEditorHandler() { _openDocumentEditor = nullptr; }
    [[nodiscard]] bool openDocumentEditor(EEditorDocumentKind kind, std::string key)
    {
        if (!_openDocumentEditor) {
            return false;
        }
        return _openDocumentEditor(kind, std::move(key));
    }
    void revealInContentBrowser(std::string vfsPath)
    {
        _pendingContentReveal = std::move(vfsPath);
        if (_showContentBrowser) {
            _showContentBrowser();
        }
    }
    [[nodiscard]] std::string consumePendingContentReveal()
    {
        std::string path;
        path.swap(_pendingContentReveal);
        return path;
    }
};

} // namespace ya
