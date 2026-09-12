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
struct UICanvasPanel;
struct UIBorder;
struct UISelectableRow;
struct UISplitPane;
struct UIScrollViewport;
struct UIText;
struct UITextField;
struct WidgetTree;
enum class EWidgetRouteResult : uint8_t;
} // namespace ya

namespace guiworkbench
{

class YA_GUI_API FWorkbenchSurface
{
  public:
    /// App-provided demo page builder: builds one page into `parent` (the
    /// content host). `log` appends to the status line.
    using FPageBuilder = std::function<void(ya::WidgetTree& tree,
                                            ya::UICanvasPanel& parent,
                                            const std::function<void(const std::string&)>& log)>;
    using FPageLeave = std::function<void(ya::WidgetTree& tree)>;

    /// Register an app page under a gallery group. The left rail shows group
    /// headers and page rows; `--start-page` matches `name`. Call before buildUI().
    int addPage(const std::string& group, const std::string& name, FPageBuilder builder);
    /// Ungrouped overload (empty group, or the last group set by addPage).
    int addPage(const std::string& name, FPageBuilder builder);
    /// Index of the built-in Editor reference page (Composition / Editor).
    [[nodiscard]] int getEditorPageIndex() const { return _editorPageIndex; }
    /// Switch the content page. Public so apps can drive the shell
    /// (automation, commands).
    void selectPage(int index);
    bool selectPageByName(const std::string& name);
    /// Called when leaving a page (before the demo host is cleared). Dock
    /// uses this to detach Popup-layer floating hosts that outlive DemoHost.
    void setPageLeave(const std::string& name, FPageLeave leave);
    /// App-owned dark/light swap. View menu items invoke this.
    std::function<void(bool bDark)> onToggleTheme;
    bool                            bDarkTheme = true;
    [[nodiscard]] int getCurrentPageIndex() const { return _currentPageIndex; }
    /// Current status-line text (app automation asserts on it).
    [[nodiscard]] const std::string& getStatusText() const;
    /// Shell chrome access for app-driven automation.
    [[nodiscard]] ya::UIMenuBar* getMenuBar() const { return _menuBar.get(); }
    [[nodiscard]] ya::UISelectableRow* getPageRow(const std::string& name) const;
    [[nodiscard]] int findPageIndexByName(const std::string& name) const;
    void setInitialPageIndex(int index) { _initialPageIndex = index; }

    FWorkbenchWorkspace workspace;

    /// Safe-zone for Hybrid chrome: menu sits to the right of traffic lights
    /// and left of the trailing window-drag gutter. Tooling does not include
    /// GUIWindowChrome.h; the host converts layout → this struct.
    struct FChromeSafeZone
    {
        float left         = 0.0f;
        float right        = 0.0f;
        float titleHeight  = 0.0f;
    };
    void applyChromeSafeZone(const FChromeSafeZone& zone);

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
    void buildMenuBar(ya::WidgetTree& tree, ya::UIContainer& parent);
    void buildPageRail(ya::WidgetTree& tree, ya::UISplitPane& parent);
    void buildPageList(ya::WidgetTree& tree, ya::UIBorder& parent);
    void syncRailSelection();
    void buildDemoHost(ya::WidgetTree& tree, ya::UISplitPane& parent);
    void buildStatusBar(ya::WidgetTree& tree, ya::UIContainer& parent);
    void buildWorkspaceShell(ya::WidgetTree& tree, ya::UIContainer& parent);
    void assembleChrome(ya::WidgetTree& tree, ya::UICanvasPanel& parent);
    void clearDemoHost();
    void logStatus(const std::string& text);

    // Editor demo page (the original workbench editor loop).
    void buildEditorDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent);
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

    std::shared_ptr<ya::UICanvasPanel>  _root;
    std::shared_ptr<ya::UIContainer>    _chromeColumn;
    FChromeSafeZone                     _chromeSafeZone;
    std::shared_ptr<ya::UIButton> _addButton;
    std::shared_ptr<ya::UIButton> _removeButton;
    std::shared_ptr<ya::UIButton> _renameButton;
    std::shared_ptr<ya::UIButton> _resetButton;
    std::shared_ptr<ya::UIText>   _statusText;
    std::shared_ptr<ya::UIText>   _commandResultText;
    std::shared_ptr<ya::UIMenuBar> _menuBar;
    std::shared_ptr<ya::UISplitPane> _workspaceSplit;
    std::shared_ptr<ya::UIBorder> _contentFrame;

    std::shared_ptr<ya::UIBorder>          _pageRailCard;
    std::shared_ptr<ya::UIScrollViewport> _pageRailScroll;
    std::shared_ptr<ya::UIContainer>      _pageRailList;

    std::vector<std::shared_ptr<ya::UISelectableRow>> _pageRows;
    std::shared_ptr<ya::UICanvasPanel>   _demoHost;
    struct FPage
    {
        std::string    group;
        std::string    name;
        FPageBuilder   build;
        FPageLeave     leave;
    };
    std::vector<FPage> _pages;
    std::string        _lastPageGroup;
    int                _editorPageIndex = -1;
    int                _currentPageIndex = -1;
    int                _initialPageIndex = 0;

    std::shared_ptr<ya::UISplitPane>      _mainSplit;
    std::shared_ptr<ya::UISplitPane>      _rightSplit;
    std::shared_ptr<ya::UICanvasPanel>          _listPanel;
    std::shared_ptr<ya::UIScrollViewport> _rowScroll;
    std::shared_ptr<ya::UIContainer>      _rowList;
    std::vector<std::shared_ptr<ya::UISelectableRow>> _rows;

    std::shared_ptr<ya::UICanvasPanel> _canvasPanel;
    std::shared_ptr<ya::UIBorder> _highlightPanel;
    std::shared_ptr<ya::UIText>  _previewName;

    std::shared_ptr<ya::UITextField> _nameField;
    std::shared_ptr<ya::UIButton>    _visibleToggle;
    std::shared_ptr<ya::UIButton>    _colorCycle;
    std::shared_ptr<ya::UIButton>    _sizeGrow;
    std::shared_ptr<ya::UIButton>    _sizeShrink;
    std::shared_ptr<ya::UIText>      _colorValue;
    std::shared_ptr<ya::UIText>      _sizeValue;

    bool _bRowsDirty = true;
};

} // namespace guiworkbench
