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
#include "GameEditor/UI/EditorAssetPicker.h"
#include "GameEditor/UI/EditorTabRegistry.h"
#include "GameEditor/UI/EditorInspectorTab.h"
#include "GameEditor/UI/EditorViewportHost.h"

#include <functional>

#include <array>
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
class RuntimeDiagnosticsSection;
class RuntimeRenderSettingsSection;
class RuntimeProfilingSection;
class RuntimeRenderGraphSection;
class RuntimeRenderTargetSection;
class RuntimeDebugPrimitivesSection;
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
    std::shared_ptr<UIText> _assetInspectorPathText;
    std::shared_ptr<UIText> _assetInspectorStatusText;
    std::shared_ptr<UIImage> _assetInspectorPreview;
    std::shared_ptr<UIText> _uiDesignerStatusText;
    std::shared_ptr<UIText> _uiDesignerSelectionText;
    std::shared_ptr<UIButton> _uiDesignerNewButton;
    std::shared_ptr<UIButton> _uiDesignerSaveButton;
    std::shared_ptr<UIButton> _uiDesignerCloseButton;
    std::shared_ptr<ReactiveList<UITreeView::FNode>> _uiDesignerRoots;
    std::shared_ptr<Reactive<std::string>> _uiDesignerSelection;
    std::shared_ptr<UITreeView> _uiDesignerTree;
    std::string _uiDesignerTreeFingerprint;
    std::shared_ptr<UIText> _runtimeToolsStatusText;
    std::shared_ptr<UIText> _runtimeToolsFrameText;
    std::shared_ptr<UIButton> _runtimeToolsPlayButton;
    std::shared_ptr<UIButton> _runtimeToolsSimulateButton;
    std::shared_ptr<UIButton> _runtimeToolsStopButton;
    std::shared_ptr<RuntimeDiagnosticsSection> _runtimeToolsDiagnostics;
    std::shared_ptr<RuntimeRenderSettingsSection> _runtimeToolsRenderSettings;
    std::shared_ptr<RuntimeProfilingSection> _runtimeToolsProfiling;
    std::shared_ptr<RuntimeRenderGraphSection> _runtimeToolsRenderGraph;
    std::shared_ptr<RuntimeRenderTargetSection> _runtimeToolsRenderTargets;
    std::shared_ptr<RuntimeDebugPrimitivesSection> _runtimeToolsDebugPrimitives;

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

    std::shared_ptr<UIPopupOverlay> _assetPickerOverlay;
    std::shared_ptr<UIPanel>        _assetPickerPanel;
    std::shared_ptr<FileExplorer>   _assetPickerExplorer;
    std::shared_ptr<UIText>         _assetPickerPathText;
    std::shared_ptr<UIText>         _assetPickerPreviewText;
    std::shared_ptr<UIButton>       _assetPickerSelectButton;
    std::shared_ptr<UIContainer>    _assetPickerMountList;
    std::shared_ptr<UIContainer>    _assetPickerEntryList;
    std::unique_ptr<UIKeyedChildReconciler> _assetPickerMountReconciler;
    std::unique_ptr<UIKeyedChildReconciler> _assetPickerEntryReconciler;
    std::string                     _assetPickerFingerprint;
    bool                            _bAssetPickerRowsDirty = true;
    EEditorAssetPickerKind          _assetPickerKind = EEditorAssetPickerKind::Texture;
    std::function<void(std::string)> _assetPickerOnPicked;

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
    [[nodiscard]] bool shouldRenderViewportGizmo() const;
    void presentViewportGizmo(ICommandBuffer& commandBuffer);
    void openSceneSaveDialog();
    void openAssetPickerDialog(EEditorAssetPickerKind kind,
                               std::string currentPath,
                               std::function<void(std::string)> onPicked);

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
    std::shared_ptr<UIElement> buildAssetInspector(EditorLayer& layer);
    std::shared_ptr<UIElement> buildUIDesigner(EditorLayer& layer);
    std::shared_ptr<UIElement> buildRuntimeTools(EditorLayer& layer);
    void syncContentBrowser();
    void rebuildContentRows();
    void selectContentMount(const std::string& itemId);
    void selectContentItem(const std::filesystem::path& path, bool bIsDirectory);
    void activateContentItem(const std::filesystem::path& path, bool bIsDirectory);
    void clearSceneSaveDialog();
    void syncSceneSaveDialog();
    void rebuildSceneSaveRows();
    void selectSceneSaveMount(const std::string& itemId);
    void activateSceneSaveItem(const std::filesystem::path& path, bool bIsDirectory);
    void confirmSceneSaveDialog();
    void clearAssetPickerDialog();
    void syncAssetPickerDialog();
    void rebuildAssetPickerRows();
    void selectAssetPickerMount(const std::string& itemId);
    void selectAssetPickerItem(const std::filesystem::path& path, bool bIsDirectory);
    void activateAssetPickerItem(const std::filesystem::path& path, bool bIsDirectory);
    void confirmAssetPickerDialog();
    void publishViewportRect();
    void syncViewportHostState(App& app);
    void applyWindowMetrics(App& app);
};

} // namespace ya
