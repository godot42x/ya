#pragma once

#include "Core/Common/Types.h"
#include "Core/Event.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Binding/SelectionModel.h"
#include "GameEditor/UI/Dialogs/EditorAssetPicker.h"
#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "GameEditor/UI/Dialogs/EditorFilePicker.h"
#include "GameEditor/UI/Shell/EditorRootSession.h"
#include "GameEditor/UI/Shell/EditorSurfaceContext.h"
#include "GameEditor/UI/Viewport/EditorViewportHost.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ya
{

struct IRenderSurfaceContext;
struct App;
struct EditorLayer;
struct EditorRootSession;
class EditorViewportGizmoOverlay;
struct Texture;
class EditorFilePickerDialog;
class EditorSettingsDialog;
struct UIDockSpace;
struct FDockContext;
struct UIDockFloatingHost;
struct UIMenu;
struct UIMenuBar;
struct UITabBar;
struct UICanvasPanel;
struct UITheme;
struct WidgetTree;
struct IImage;
struct IImageView;
struct FEditorProjectBrowser;
struct UIDragDropOperation;
enum class EWidgetRouteResult : uint8_t;

/// Game Editor chrome owned as one WidgetTree. Per-window orchestrator, not
/// a product loop: `EditorModule::onPresentation` calls
/// `EditorWindowSession::tick` → `EditorSurface::tick`.
///
/// tick: rebuild-if-needed -> window metrics -> WidgetTree::tick ->
/// shell chrome -> pushViewportDisplay -> buildSnapshot -> viewport host bridge.
/// rebuild: new tree/theme/dock -> shell chrome -> spawn tabs into FDockContext.
struct EditorSurface : IEditorViewportHostSink
{
  private:
    EditorLayer* _layer = nullptr;
    std::unique_ptr<WidgetTree> _tree;
    /// Cached from the per-frame FEditorSurfaceContext, same lifetime contract as
    /// _presentSurface: the host owns the App and clears it on unbind. Needed by
    /// the settings dialog, which mutates live resources (a font-face switch
    /// reloads the stack) between frames.
    App* _app = nullptr;
    std::shared_ptr<UITheme>    _theme;
    UIFrameSnapshot             _snapshot;
    bool                        _bBuiltAsProjectBrowser = false;

    std::shared_ptr<UICanvasPanel>         _root;
    std::shared_ptr<UICanvasPanel>         _titleBar;
    std::shared_ptr<UITabBar>        _pageTabBar;
    std::vector<std::string>         _pageTabKeys;
    std::shared_ptr<UIMenuBar>       _menuBar;
    std::shared_ptr<FDockContext>    _dockContext;
    std::shared_ptr<FDockContext>    _ownedDockContext;
    std::shared_ptr<UIDockSpace>     _dockSpace;
    std::shared_ptr<UIDockFloatingHost> _dockFloatingHost;
    std::shared_ptr<UIDockFloatingHost> _ownedDockFloatingHost;
    std::unique_ptr<FEditorProjectBrowser> _projectBrowser;
    std::shared_ptr<SelectionModel>  _projectSelection;
    EditorRootSession*               _rootSession = nullptr;
    FEditorRootSessions              _roots;
    EditorWindowId                   _windowId    = kDefaultEditorWindowId;
    EditorTabSpawnerRegistry*        _tabSpawners = nullptr;
    EditorDocumentRegistry*          _documents   = nullptr;
    IRenderSurfaceContext*           _presentSurface = nullptr;
    std::function<void()>            _persistLayout;
    std::function<bool(FDockContext&, uint64_t, const glm::vec2&, const glm::vec2&)> _onDockNoTargetTearOff;
    EditorDockWorkspace              _workspace;
    EditorDockWorkspace              _ownedWorkspace;

    std::unique_ptr<EditorFilePickerDialog> _filePicker;
    std::unique_ptr<EditorSettingsDialog> _settings;

    std::shared_ptr<Texture>    _viewportTexture;
    std::shared_ptr<IImage>     _viewportImageResource;
    std::shared_ptr<IImageView> _viewportImageView;

    EditorViewportOverlayHost _viewportOverlayHost;
    std::shared_ptr<EditorViewportGizmoOverlay> _viewportGizmoOverlay;
    IEditorViewportHost* _viewportHost = nullptr;
    std::shared_ptr<UIMenu> _viewportContextMenu;
    bool                    _bViewportRightPressPending = false;
    glm::vec2               _viewportRightPressPos{};

  public:
    EditorSurface();
    ~EditorSurface();

    void bind(EditorLayer& layer,
              EditorTabSpawnerRegistry* spawners = nullptr,
              EditorRootSession* root = nullptr,
              EditorWindowId windowId = kDefaultEditorWindowId,
              EditorDocumentRegistry* documents = nullptr,
              FEditorRootSessions roots = {})
    {
        _layer = &layer;
        _tabSpawners = spawners;
        _rootSession = root;
        _windowId = windowId;
        _documents = documents;
        _roots = roots;
    }
    void setPersistLayout(std::function<void()> fn) { _persistLayout = std::move(fn); }
    void setOnDockNoTargetTearOff(
        std::function<bool(FDockContext&, uint64_t, const glm::vec2&, const glm::vec2&)> fn);
    void unbind()
    {
        _layer = nullptr;
        _tabSpawners = nullptr;
        _rootSession = nullptr;
        _documents = nullptr;
        _roots = {};
        _windowId = kDefaultEditorWindowId;
        _presentSurface = nullptr;
    }
    void shutdown();

    void tick(const FEditorSurfaceContext& context, float dt);
    [[nodiscard]] const UIFrameSnapshot& snapshot() const { return _snapshot; }

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& windowPoint);
    [[nodiscard]] bool isViewportHovered() const;
    [[nodiscard]] bool isViewportFocused() const;
    [[nodiscard]] bool isPointInViewport(const glm::vec2& windowPoint) const;
    [[nodiscard]] bool wantsTextInput() const;
    [[nodiscard]] WidgetTree* tree() const { return _tree.get(); }
    [[nodiscard]] FDockContext* windowRootDock() const { return _dockContext.get(); }
    [[nodiscard]] FDockContext* ownedNestedDock() const { return _ownedDockContext.get(); }
    [[nodiscard]] std::shared_ptr<FDockContext> windowRootDockPtr() const { return _dockContext; }
    [[nodiscard]] std::shared_ptr<FDockContext> ownedNestedDockPtr() const { return _ownedDockContext; }
    [[nodiscard]] EditorViewportOverlayHost& viewportOverlayHost() { return _viewportOverlayHost; }
    [[nodiscard]] const EditorViewportOverlayHost& viewportOverlayHost() const { return _viewportOverlayHost; }
    [[nodiscard]] bool isViewportOverlayActive() const { return _viewportOverlayHost.isActive(); }
    void setViewportHost(IEditorViewportHost* host) override { _viewportHost = host; }
    void openSceneSaveDialog();
    void openFilePickerDialog(FEditorFilePickerRequest request);
    void openAssetPickerDialog(EEditorAssetPickerKind kind,
                               std::string currentPath,
                               std::function<void(std::string)> onPicked);
    void openEditorSettingsDialog();
    void showContentBrowser();
    bool invokeTab(std::string_view tabId);
    bool openDocumentEditor(EEditorDocumentKind kind, std::string key);

  private:
    void rebuild(const FEditorSurfaceContext& context);
    void buildProjectBrowser(App& app);
    void buildEditorChrome(const FEditorSurfaceContext& context);
    void syncShellDialogs();
    void pushViewportDisplay();
    void refreshProjectBrowserRows();
    void publishViewportRect();
    void syncViewportHostState(const FEditorSurfaceContext& context);
    void applyWindowMetrics(const EditorWindowMetrics& metrics);
    void persistDockLayouts();
    void installDockNoTargetTearOff();
    /// Double-click on the empty page-tab strip (not a tab button, not a
    /// dock leaf tab bar) toggles native title-bar zoom. The TabBar rect is
    /// Client so this callback actually receives the event; trailing gutter
    /// Drag still goes through GUIWindowChrome.
    void installEmptyTabBarZoom();
    void publishTitleClientHits();
    void syncPageTabs();
    void beginPageTabDrag(int index);
    [[nodiscard]] bool acceptPageTabDrop(const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    void dropOntoPageTabs(const UIDragDropOperation& operation);
    void openViewportContextMenu(const glm::vec2& windowPoint);
    void closeViewportContextMenu();
};

} // namespace ya
