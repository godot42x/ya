#pragma once

#include "Core/Common/Types.h"
#include "Core/Event.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Binding/SelectionModel.h"
#include "GUI/Binding/ActionMap.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Widgets/Controls/TreeView.h"

#include "GameEditor/UI/EditorAssetPicker.h"
#include "GameEditor/UI/EditorFilePicker.h"
#include "GameEditor/UI/EditorViewportHost.h"

#include <functional>
#include <memory>
#include <string>

namespace guiworkbench
{
class FWorkbenchSurface;
}

namespace ya
{

struct ICommandBuffer;

struct App;
struct EditorLayer;
class EditorViewportGizmoOverlay;
struct Texture;
class EditorFilePickerDialog;
class EditorSettingsDialog;
class EditorInspectorTab;
class EditorDebugImagesTab;
class EditorContentBrowserTab;
class EditorAssetInspectorTab;
class EditorUIDesignerTab;
class EditorRuntimeToolsTab;
struct UIDockSpace;
struct FDockContext;
struct UIDockFloatingHost;
struct UIElement;
struct UIContainer;
struct UIButton;
struct UIImage;
struct UIMenuBar;
struct UIPanel;
struct UIPopupOverlay;
struct UIScrollViewport;
struct UISizeBox;
struct UIText;
struct UITextField;
struct UITheme;
struct UITreeView;
struct WidgetTree;
struct IImage;
struct IImageView;
enum class EWidgetRouteResult : uint8_t;

/// Game Editor chrome owned as one WidgetTree.
///
/// tick: rebuild-if-needed -> window metrics -> sync tabs/chrome ->
/// buildSnapshot -> viewport host.
/// rebuild: new tree/theme/dock -> shell chrome -> tab.build() into FDockContext.
/// Tab content lives on owner objects; this surface keeps shell, dock persist,
/// viewport host, and dialogs.
struct EditorSurface
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
    std::shared_ptr<UIImage>         _viewportImage;
    std::shared_ptr<UITreeView>      _hierarchyView;
    std::shared_ptr<ReactiveList<UITreeView::FNode>> _hierarchyRoots;
    std::shared_ptr<Reactive<std::string>>           _hierarchyFilter;
    std::shared_ptr<UITextField>                     _hierarchyFilterField;
    std::shared_ptr<SelectionModel>  _selection = std::make_shared<SelectionModel>();
    std::shared_ptr<ActionMap>       _actions   = std::make_shared<ActionMap>();
    std::shared_ptr<UndoStack>       _undo      = std::make_shared<UndoStack>();
    std::shared_ptr<UIText>          _statsText;
    std::unique_ptr<EditorInspectorTab> _inspectorTab;
    std::unique_ptr<EditorDebugImagesTab> _debugImagesTab;
    std::unique_ptr<EditorContentBrowserTab> _contentBrowserTab;
    std::unique_ptr<EditorAssetInspectorTab> _assetInspectorTab;
    std::unique_ptr<EditorUIDesignerTab> _uiDesignerTab;
    std::unique_ptr<EditorRuntimeToolsTab> _runtimeToolsTab;
    std::unique_ptr<guiworkbench::FWorkbenchSurface> _workbench;

    std::unique_ptr<EditorFilePickerDialog> _filePicker;
    std::unique_ptr<EditorSettingsDialog> _settings;

    std::shared_ptr<Texture>    _viewportTexture;
    std::shared_ptr<IImage>     _viewportImageResource;
    std::shared_ptr<IImageView> _viewportImageView;

    std::string _hierarchyFingerprint;
    uint64_t    _syncedSelectionGeneration = ~uint64_t{0};
    EditorViewportOverlayHost _viewportOverlayHost;
    std::shared_ptr<EditorViewportGizmoOverlay> _viewportGizmoOverlay;

  public:
    EditorSurface();
    ~EditorSurface();

    void bind(EditorLayer& layer) { _layer = &layer; }
    void unbind() { _layer = nullptr; }
    /// Drop the tree, snapshot, and GPU-backed viewport wrap before VMA teardown.
    void shutdown();

    void tick(App& app, float dt);
    [[nodiscard]] const UIFrameSnapshot& snapshot() const { return _snapshot; }

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& windowPoint);
    [[nodiscard]] bool isViewportHovered() const;
    [[nodiscard]] bool isViewportFocused() const;
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
    void openSceneSaveDialog();
    void openFilePickerDialog(FEditorFilePickerRequest request);
    void openAssetPickerDialog(EEditorAssetPickerKind kind,
                               std::string currentPath,
                               std::function<void(std::string)> onPicked);
    void openEditorSettingsDialog();

  private:
    void rebuild(App& app);
    void buildProjectBrowser(App& app);
    void buildEditorChrome(App& app);
    void registerEditorActions();
    void syncPresentation(App& app, float dt);
    void syncViewportTexture();
    void syncHierarchy();
    void syncSelectionFromLayer();
    void syncToolbar(App& app);
    void publishViewportRect();
    void syncViewportHostState(App& app);
    void applyWindowMetrics(App& app);
    void applyDefaultEditorDockLayout();
    bool tryRestoreEditorDockLayout();
    void persistEditorDockLayout();
    void openViewportContextMenu(const glm::vec2& windowPoint);
};

} // namespace ya
