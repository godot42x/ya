#pragma once

#include "Core/Event.h"
#include "GUI/Tooling/Workbench/WorkbenchWorkspace.h"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{
struct UIButton;
struct UIContainer;
struct UIElement;
struct UIMenuBar;
struct UIPanel;
struct UISelectableRow;
struct UISplitPane;
struct UIScrollViewport;
struct UITabBar;
struct UIText;
struct UITextField;
struct WidgetTree;
enum class EWidgetRouteResult : uint8_t;
} // namespace ya

namespace guiworkbench
{

class FWorkbenchSurface
{
  public:
    /// App-provided demo page builder: builds one page into `parent` (the
    /// content host). `log` appends to the status line.
    using FPageBuilder = std::function<void(ya::WidgetTree& tree,
                                            ya::UIElement& parent,
                                            const std::function<void(const std::string&)>& log)>;

    /// Register an app page (appends a tab). Pages are example/app content:
    /// the shell only knows their names and builders. Call before buildUI().
    int addPage(const std::string& name, FPageBuilder builder);
    /// Index of the built-in Editor reference page (after all registered
    /// pages; the shell's own tool-GUI example, shared with the product
    /// editor panel).
    [[nodiscard]] int getEditorPageIndex() const { return _editorPageIndex; }
    /// Switch the content page (tabs + content host). Public so apps can
    /// drive the shell (automation, commands).
    void selectPage(int index);
    [[nodiscard]] int getCurrentPageIndex() const { return _currentPageIndex; }
    /// Current status-line text (app automation asserts on it).
    [[nodiscard]] const std::string& getStatusText() const;
    /// Shell chrome access for app-driven automation.
    [[nodiscard]] ya::UIMenuBar* getMenuBar() const { return _menuBar.get(); }
    [[nodiscard]] ya::UITabBar*  getTabBar() const { return _tabBar.get(); }
    [[nodiscard]] int findPageIndexByName(const std::string& name) const;
    void setInitialPageIndex(int index) { _initialPageIndex = index; }

    FWorkbenchWorkspace workspace;

    void buildUI(ya::WidgetTree& tree);
    void updateUI();
    void onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result);

    void setSmokeActionsEnabled(bool bEnabled) { _bSmokeActions = bEnabled; }
    [[nodiscard]] bool isSmokeActionsEnabled() const { return _bSmokeActions; }
    [[nodiscard]] bool getSmokePassed() const { return _bSmokePassed; }
    /// App-driven automation step for app-registered pages (called on every
    /// smoke frame BEFORE the built-in editor automation). Return true when
    /// the frame was handled by the app, false to fall through to the
    /// built-in editor steps. Use failSmoke() to abort.
    std::function<bool(int frame)> externalAutomationStep;
    /// Abort the current smoke run with `message` (used by app automation).
    void failSmoke(const std::string& message);

  private:
    void buildMenuBar(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildTabBar(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildDemoHost(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildStatusBar(ya::WidgetTree& tree, ya::UIElement& parent);
    void clearDemoHost();
    void logStatus(const std::string& text);

    // Editor demo page (the original workbench editor loop).
    void buildEditorDemo(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildToolbar(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildDocumentList(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildCanvas(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildInspector(ya::WidgetTree& tree, ya::UIElement& parent);

    void rebuildItemRows();
    void syncPresentationState();

    void cmdAdd();
    void cmdRemove();
    void cmdRename();
    void cmdResetLayout();
    void setCommandResult(const std::string& text);

    void handleUnhandledKey(const ya::KeyPressedEvent& keyEvent);
    void runAutomation();
    void dispatchPointer(const ya::Event& event, const glm::vec2& point);
    void dispatchKey(const ya::Event& event);

    ya::WidgetTree* _tree = nullptr;
    uint64_t        _frame = 0;
    bool            _bSmokeActions = false;
    bool            _bSmokePassed = false;
    bool            _bAutomationDone = false;

    std::shared_ptr<ya::UIPanel>  _root;
    std::shared_ptr<ya::UIButton> _addButton;
    std::shared_ptr<ya::UIButton> _removeButton;
    std::shared_ptr<ya::UIButton> _renameButton;
    std::shared_ptr<ya::UIButton> _resetButton;
    std::shared_ptr<ya::UIText>   _statusText;
    std::shared_ptr<ya::UIText>   _commandResultText;
    std::shared_ptr<ya::UIMenuBar> _menuBar;
    std::shared_ptr<ya::UITabBar>  _tabBar;
    std::shared_ptr<ya::UIPanel>   _demoHost;
    struct FPage
    {
        std::string  name;
        FPageBuilder build;
    };
    std::vector<FPage> _pages;
    int                _editorPageIndex = -1;
    int                _currentPageIndex = -1;
    int                _initialPageIndex = 0;

    std::shared_ptr<ya::UISplitPane>      _mainSplit;
    std::shared_ptr<ya::UISplitPane>      _rightSplit;
    std::shared_ptr<ya::UIPanel>          _listPanel;
    std::shared_ptr<ya::UIScrollViewport> _rowScroll;
    std::shared_ptr<ya::UIContainer>      _rowList;
    std::vector<std::shared_ptr<ya::UISelectableRow>> _rows;

    std::shared_ptr<ya::UIPanel> _canvasPanel;
    std::shared_ptr<ya::UIPanel> _highlightPanel;
    std::shared_ptr<ya::UIText>  _previewName;

    std::shared_ptr<ya::UITextField> _nameField;
    std::shared_ptr<ya::UIButton>    _visibleToggle;
    std::shared_ptr<ya::UIButton>    _colorCycle;
    std::shared_ptr<ya::UIButton>    _sizeGrow;
    std::shared_ptr<ya::UIButton>    _sizeShrink;
    std::shared_ptr<ya::UIText>      _colorValue;
    std::shared_ptr<ya::UIText>      _sizeValue;

    bool _bRowsDirty = true;

    // Last-synced geometry-affecting presentation state; syncPresentationState
    // invalidates layout only when these change (SizeToContent widgets need a
    // relayout when their runtime label / size differs from the last layout).
    std::string _lastToggleText;
    glm::vec2   _lastHighlightSize{0.0f};
    glm::vec2   _lastHighlightPos{0.0f};
};

} // namespace guiworkbench
