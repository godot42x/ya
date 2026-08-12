#pragma once

#include "Core/Event.h"
#include "GUI/Tooling/Workbench/WorkbenchWorkspace.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ya
{
struct UIButton;
struct UIContainer;
struct UIElement;
struct UIPanel;
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

class FWorkbenchSurface
{
  public:
    FWorkbenchWorkspace workspace;

    void buildUI(ya::WidgetTree& tree);
    void updateUI();
    void onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result);

    void setSmokeActionsEnabled(bool bEnabled) { _bSmokeActions = bEnabled; }
    [[nodiscard]] bool isSmokeActionsEnabled() const { return _bSmokeActions; }
    [[nodiscard]] bool getSmokePassed() const { return _bSmokePassed; }

  private:
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
};

} // namespace guiworkbench
