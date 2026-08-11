#pragma once

// ============================================================================
// FWorkbenchApp - GUIWorkbench delegate (gui-app-bootstrap Phase 3).
//
// Layering:
//   FWorkbenchWorkspace - document / selection / command fact source
//   FWorkbenchApp       - presenter: maps workspace -> widget state, turns
//                         widget actions into workspace mutations
//   GUIAppHost          - window / input / snapshot / present (engine)
//
// The shell is a real tool layout built only from GUI framework controls:
// toolbar commands | document list | preview canvas | inspector. No ImGui.
// ============================================================================

#include "GUI/App/GUIAppHost.h"

#include "GUIWorkbenchWorkspace.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ya
{
struct UIPanel;
struct UIButton;
struct UIContainer;
struct UISelectableRow;
struct UISplitPane;
struct UIText;
struct UITextField;
struct UIScrollViewport;
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

    /// Headless end-to-end smoke: drives the real event path (dispatch +
    /// routed-result observation) across selection / inspector / command /
    /// keyboard navigation, then asserts the workspace state. Enabled via
    /// `--smoke-actions`; the process exit code reflects the result.
    bool bSmokeActions = false;
    [[nodiscard]] bool getSmokePassed() const { return _bSmokePassed; }

  private:
    void buildToolbar(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildDocumentList(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildCanvas(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildInspector(ya::WidgetTree& tree, ya::UIElement& parent);

    void rebuildDocumentRows(ya::WidgetTree& tree);
    void syncPresentationState();
    void handleUnhandledKey(const ya::KeyPressedEvent& keyEvent);
    void setCommandResult(const std::string& text);
    void runAutomation();
    void dispatchPointer(const ya::Event& event, const glm::vec2& point);
    void dispatchKey(const ya::Event& event);

    ya::WidgetTree* _tree = nullptr;
    uint64_t        _frame = 0;
    bool            _bSmokePassed = false;
    bool            _bAutomationDone = false;

    // Shell handles (strong refs mirror the tree's ownership).
    std::shared_ptr<ya::UIPanel>       _root;
    std::shared_ptr<ya::UIButton>      _newButton;
    std::shared_ptr<ya::UIButton>      _openButton;
    std::shared_ptr<ya::UIButton>      _saveButton;
    std::shared_ptr<ya::UIButton>      _reloadButton;
    std::shared_ptr<ya::UIText>        _statusText;
    std::shared_ptr<ya::UIText>        _commandResultText;

    // Document list.
    std::shared_ptr<ya::UIContainer>   _rowList; // rebuilt on document commands
    std::shared_ptr<ya::UIScrollViewport> _rowScroll;
    std::vector<std::shared_ptr<ya::UISelectableRow>> _rows;

    // Preview canvas.
    std::shared_ptr<ya::UIText>        _previewName;
    std::shared_ptr<ya::UIPanel>       _highlightPanel;

    // Inspector.
    std::shared_ptr<ya::UITextField>   _nameField;
    std::shared_ptr<ya::UIText>        _visibleValue;
    std::shared_ptr<ya::UIButton>      _visibleToggle;
    std::shared_ptr<ya::UIText>        _colorValue;
    std::shared_ptr<ya::UIButton>      _colorCycle;
    std::shared_ptr<ya::UIText>        _sizeValue;
    std::shared_ptr<ya::UIButton>      _sizeGrow;
    std::shared_ptr<ya::UIButton>      _sizeShrink;

    bool _bRowsDirty = true;
};

} // namespace guiworkbench
