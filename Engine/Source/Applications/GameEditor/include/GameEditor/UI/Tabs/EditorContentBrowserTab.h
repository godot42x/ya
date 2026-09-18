#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/KeyedChildReconciler.h"

#include "GameEditor/FileExplorer.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
struct UIElement;
struct WidgetTree;
struct UISplitPane;

/// Retained Content Browser tab. FileExplorer owns mount/directory/filter
/// state; this tab is the WidgetTree view (keyed rows + visible window).
class EditorContentBrowserTab : public UICompoundWidget
{
  public:
    explicit EditorContentBrowserTab(EditorLayer& layer);

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;

    std::shared_ptr<class FileExplorer> _explorer;
    std::shared_ptr<struct UIText> _pathText;
    std::shared_ptr<struct UITextField> _searchField;
    std::shared_ptr<struct UISelectableRow> _listModeToggle;
    std::shared_ptr<struct UISelectableRow> _gridModeToggle;
    std::shared_ptr<struct UIContainer> _mountList;
    std::shared_ptr<struct UIContainer> _entryList;
    std::shared_ptr<struct UIContainer> _entryRows;
    std::shared_ptr<struct UISizeBox> _entryLeading;
    std::shared_ptr<struct UISizeBox> _entryTrailing;
    std::shared_ptr<struct UIScrollViewport> _entryScroll;
    std::shared_ptr<UISplitPane> _bodySplit;
    std::unique_ptr<UIKeyedChildReconciler> _mountReconciler;
    std::unique_ptr<UIKeyedChildReconciler> _entryReconciler;
    uint64_t _explorerGeneration = 0;
    std::string _selectedFingerprint;
    float _entryScrollOffset = 0.0f;
    float _entryViewportHeight = 0.0f;
    float _entryViewportWidth = 0.0f;
    int _entryViewportWidthPx = -1;
    int _entryViewportHeightPx = -1;
    int _entryScrollOffsetPx = -1;
    int _gridColumnCount = 0;
    bool _bRowsDirty = true;
    bool _bSelectionDirty = false;
    bool _bSplitRatioFromConfig = false;
    FileExplorer::ViewMode _reconciledViewMode = FileExplorer::ViewMode::List;

    void refresh();
    void refreshFromTree(WidgetTree& tree);
    void rebuildRows(WidgetTree& tree);
    void applySelection();
    void selectMount(const std::string& itemId);
    void selectItem(const std::filesystem::path& path, bool bIsDirectory);
    void activateItem(const std::filesystem::path& path, bool bIsDirectory);
    void setViewMode(FileExplorer::ViewMode mode);
    void syncGridRowTiles(WidgetTree& tree,
                          UIElement& row,
                          const std::vector<FileExplorer::FEntry>& entries,
                          size_t begin,
                          size_t count,
                          const std::filesystem::path& selectedPath,
                          float cellW,
                          float rowH,
                          float thumbnailSize);
    [[nodiscard]] std::string entryIconPath(const FileExplorer::FEntry& entry) const;
};

} // namespace ya
