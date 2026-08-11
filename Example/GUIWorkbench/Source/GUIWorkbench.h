#pragma once

// ============================================================================
// FWorkbenchApp - GUIWorkbench presenter (gui-app-bootstrap Phase 3).
//
// Retain-mode shell built entirely from GUI framework controls (no ImGui,
// no Scene, no .yaui): toolbar commands | item list | live preview | 
// inspector. Layering (plan 4.2):
//
//   FWorkbenchWorkspace - app state / selection / commands (no GUI include)
//   FWorkbenchApp       - the ONLY layer knowing workspace + widget ids:
//                         maps workspace -> widget state and turns widget
//                         actions back into workspace mutations
//   Widget shell        - layout / paint / transient hover/focus/pressed
//   GUIAppHost          - window / input / snapshot / present (engine)
// ============================================================================

#include "GUI/App/GUIAppHost.h"

#include "GUIWorkbenchWorkspace.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ya
{
struct UIButton;
struct UIContainer;
struct UIPanel;
struct UISelectableRow;
struct UISplitPane;
struct UIScrollViewport;
struct UIText;
struct UITextField;
struct WidgetTree;
} // namespace ya

namespace guiworkbench
{

class FWorkbenchApp final : public ya::IGUIAppDelegate
{
  public:
    FWorkbenchWorkspace workspace;

    // === IGUIAppDelegate ===
    void buildUI(ya::WidgetTree& tree) override;
    void updateUI() override;
    void onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result) override;
    [[nodiscard]] bool shouldRequestClose() const override { return _bRequestClose; }

    /// Headless end-to-end smoke (--smoke-actions): drives the real event
    /// path (dispatch + routed-result observation) across add -> select ->
    /// rename -> remove, then asserts workspace + presenter sync.
    bool bSmokeActions = false;
    [[nodiscard]] bool getSmokePassed() const { return _bSmokePassed; }

  private:
    void buildToolbar(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildDocumentList(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildCanvas(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildInspector(ya::WidgetTree& tree, ya::UIElement& parent);

    // Frame-boundary sync (updateUI).
    void rebuildItemRows();
    void syncPresentationState();

    // Commands.
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
    bool            _bSmokePassed = false;
    bool            _bAutomationDone = false;
    bool            _bRequestClose = false;

    // Shell handles.
    std::shared_ptr<ya::UIPanel>       _root;
    std::shared_ptr<ya::UIButton>      _addButton;
    std::shared_ptr<ya::UIButton>      _removeButton;
    std::shared_ptr<ya::UIButton>      _renameButton;
    std::shared_ptr<ya::UIButton>      _resetButton;
    std::shared_ptr<ya::UIText>        _statusText;
    std::shared_ptr<ya::UIText>        _commandResultText;

    // Item list.
    std::shared_ptr<ya::UISplitPane>       _mainSplit;
    std::shared_ptr<ya::UISplitPane>       _rightSplit;
    std::shared_ptr<ya::UIPanel>           _listPanel;
    std::shared_ptr<ya::UIScrollViewport>  _rowScroll;
    std::shared_ptr<ya::UIContainer>      _rowList;
    std::vector<std::shared_ptr<ya::UISelectableRow>> _rows;

    // Preview canvas.
    std::shared_ptr<ya::UIPanel>  _canvasPanel;
    std::shared_ptr<ya::UIPanel>  _highlightPanel;
    std::shared_ptr<ya::UIText>   _previewName;

    // Inspector.
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
