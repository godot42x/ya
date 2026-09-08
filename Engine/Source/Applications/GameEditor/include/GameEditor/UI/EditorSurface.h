#pragma once

#include "Core/Common/Types.h"
#include "Core/Delegate.h"
#include "Core/Event.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Binding/SelectionModel.h"
#include "GUI/Binding/ActionMap.h"
#include "GUI/Binding/UndoStack.h"

#include "GameEditor/UI/EditorAssetPicker.h"
#include "GameEditor/UI/EditorDockWorkspace.h"
#include "GameEditor/UI/EditorFilePicker.h"
#include "GameEditor/UI/EditorViewportHost.h"

#include <functional>
#include <memory>
#include <string>

namespace ya
{

struct App;
struct EditorLayer;
class EditorViewportGizmoOverlay;
struct Texture;
class EditorFilePickerDialog;
class EditorSettingsDialog;
struct UIDockSpace;
struct FDockContext;
struct UIDockFloatingHost;
struct UIMenu;
struct UIMenuBar;
struct UIPanel;
struct UIText;
struct UITheme;
struct WidgetTree;
struct IImage;
struct IImageView;
struct FEditorProjectBrowser;
enum class EWidgetRouteResult : uint8_t;

/// Game Editor chrome owned as one WidgetTree.
///
/// tick: rebuild-if-needed -> window metrics -> WidgetTree::tick ->
/// shell chrome -> buildSnapshot -> viewport host bridge.
/// rebuild: new tree/theme/dock -> shell chrome -> spawn tabs into FDockContext.
struct EditorSurface : IEditorViewportHostSink
{
  private:
    EditorLayer* _layer = nullptr;
    std::unique_ptr<WidgetTree> _tree;
    std::shared_ptr<UITheme>    _theme;
    UIFrameSnapshot             _snapshot;
    bool                        _bBuiltAsProjectBrowser = false;

    std::shared_ptr<UIPanel>         _root;
    std::shared_ptr<UIMenuBar>       _menuBar;
    std::shared_ptr<UIText>          _toolbarModeText;
    std::shared_ptr<FDockContext>    _dockContext;
    std::shared_ptr<UIDockSpace>     _dockSpace;
    std::shared_ptr<UIDockFloatingHost> _dockFloatingHost;
    std::unique_ptr<FEditorProjectBrowser> _projectBrowser;
    std::shared_ptr<SelectionModel>  _selection = std::make_shared<SelectionModel>();
    std::shared_ptr<ActionMap>       _actions   = std::make_shared<ActionMap>();
    std::shared_ptr<UndoStack>       _undo      = std::make_shared<UndoStack>();
    EditorTabSpawnerRegistry*        _tabSpawners = nullptr;
    App*                             _app = nullptr;
    DelegateHandle                   _appStateHandle = INVALID_HANDLE;
    EditorDockWorkspace              _workspace;

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

    void bind(EditorLayer& layer, EditorTabSpawnerRegistry* spawners = nullptr)
    {
        _layer = &layer;
        _tabSpawners = spawners;
    }
    void unbind()
    {
        _layer = nullptr;
        _tabSpawners = nullptr;
    }
    void shutdown();

    void tick(App& app, float dt);
    [[nodiscard]] const UIFrameSnapshot& snapshot() const { return _snapshot; }

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& windowPoint);
    [[nodiscard]] bool isViewportHovered() const;
    [[nodiscard]] bool isViewportFocused() const;
    [[nodiscard]] bool isPointInViewport(const glm::vec2& windowPoint) const;
    [[nodiscard]] bool wantsTextInput() const;
    [[nodiscard]] WidgetTree* tree() const { return _tree.get(); }
    [[nodiscard]] SelectionModel& selection() { return *_selection; }
    [[nodiscard]] const SelectionModel& selection() const { return *_selection; }
    [[nodiscard]] ActionMap& actions() { return *_actions; }
    [[nodiscard]] const ActionMap& actions() const { return *_actions; }
    [[nodiscard]] UndoStack& undo() { return *_undo; }
    [[nodiscard]] const UndoStack& undo() const { return *_undo; }
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

  private:
    void rebuild(App& app);
    void buildProjectBrowser(App& app);
    void buildEditorChrome(App& app);
    void syncShellDialogs();
    void pushViewportDisplay();
    void updateToolbarMode(App& app);
    void bindAppState(App& app);
    void unbindAppState();
    void refreshProjectBrowserRows();
    void publishViewportRect();
    void syncViewportHostState(App& app);
    void applyWindowMetrics(App& app);
    void openViewportContextMenu(const glm::vec2& windowPoint);
    void closeViewportContextMenu();
};

} // namespace ya
