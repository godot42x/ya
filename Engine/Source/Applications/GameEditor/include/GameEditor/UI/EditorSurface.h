#pragma once

#include "Core/Common/Types.h"
#include "Core/Event.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Binding/SelectionModel.h"
#include "GUI/Binding/ActionMap.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/KeyedChildReconciler.h"

#include "GameEditor/FileExplorer.h"
#include "GameEditor/UI/EditorTabRegistry.h"
#include "GameEditor/UI/EditorInspectorTab.h"

#include <array>
#include <memory>
#include <string>

namespace guiworkbench
{
class FWorkbenchSurface;
}

namespace ya
{

struct App;
struct EditorLayer;
struct Texture;
struct UIDockSpace;
struct UIDockWorkspace;
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

/// Game Editor chrome owned as one WidgetTree. The presentation host replays
/// the snapshot into the open swapchain pass; the 3D viewport remains an
/// offscreen compose that this tree samples as a live UIImage.
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
    std::shared_ptr<UIDockWorkspace> _dockWorkspace;
    std::shared_ptr<UIDockSpace>     _dockSpace;
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
    std::unique_ptr<guiworkbench::FWorkbenchSurface> _workbench;
    std::unique_ptr<EditorTabRegistry> _tabRegistry;

    // Content Browser (WidgetTree chrome). FileExplorer keeps the mount /
    // directory / filter state; the rows below are the retained view.
    std::shared_ptr<FileExplorer>  _contentExplorer;
    std::shared_ptr<UIText>        _contentPathText;
    std::shared_ptr<UITextField>   _contentSearchField;
    std::shared_ptr<UIContainer>   _contentMountList;
    std::shared_ptr<UIContainer>   _contentEntryList;
    std::shared_ptr<UIContainer>   _contentEntryRows;
    std::shared_ptr<UISizeBox>     _contentEntryLeading;
    std::shared_ptr<UISizeBox>     _contentEntryTrailing;
    std::shared_ptr<UIScrollViewport> _contentEntryScroll;
    std::unique_ptr<UIKeyedChildReconciler> _contentMountReconciler;
    std::unique_ptr<UIKeyedChildReconciler> _contentEntryReconciler;
    std::string                    _contentFingerprint;
    float                          _contentEntryScrollOffset = 0.0f;
    float                          _contentEntryViewportHeight = 0.0f;
    bool                           _bContentRowsDirty = true;

    std::shared_ptr<UIPopupOverlay> _sceneSaveOverlay;
    std::shared_ptr<UIPanel>        _sceneSavePanel;
    std::shared_ptr<FileExplorer>   _sceneSaveExplorer;
    std::shared_ptr<UIText>         _sceneSavePathText;
    std::shared_ptr<UIText>         _sceneSavePreviewText;
    std::shared_ptr<UITextField>    _sceneSaveNameField;
    std::shared_ptr<UIButton>       _sceneSaveSaveButton;
    std::shared_ptr<UIContainer>    _sceneSaveMountList;
    std::shared_ptr<UIContainer>    _sceneSaveEntryList;
    std::unique_ptr<UIKeyedChildReconciler> _sceneSaveMountReconciler;
    std::unique_ptr<UIKeyedChildReconciler> _sceneSaveEntryReconciler;
    std::string                     _sceneSaveFingerprint;
    bool                            _bSceneSaveRowsDirty = true;

    std::shared_ptr<Texture>    _viewportTexture;
    std::shared_ptr<IImage>     _viewportImageResource;
    std::shared_ptr<IImageView> _viewportImageView;

    std::string _hierarchyFingerprint;
    uint64_t    _syncedSelectionGeneration = ~uint64_t{0};

  public:
    EditorSurface() = default;
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
    std::shared_ptr<UIElement> buildContentBrowser();
    void syncContentBrowser();
    void rebuildContentRows();
    void selectContentMount(const std::string& itemId);
    void selectContentItem(const std::filesystem::path& path, bool bIsDirectory);
    void activateContentItem(const std::filesystem::path& path, bool bIsDirectory);
    void openSceneSaveDialog();
    void clearSceneSaveDialog();
    void syncSceneSaveDialog();
    void rebuildSceneSaveRows();
    void selectSceneSaveMount(const std::string& itemId);
    void activateSceneSaveItem(const std::filesystem::path& path, bool bIsDirectory);
    void confirmSceneSaveDialog();
    void publishViewportRect();
    void applyWindowMetrics(App& app);
};

} // namespace ya
