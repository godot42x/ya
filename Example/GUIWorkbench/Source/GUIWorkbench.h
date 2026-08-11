#pragma once

// ============================================================================
// FWorkbenchApp - GUIWorkbench delegate (gui-app-bootstrap).
//
// The GUI app minimal closure: a real .yaui document editor built entirely
// from GUI framework controls (no ImGui, no Scene):
//
//   toolbar commands  | document tree | live preview canvas | inspector +
//   palette
//
// Layering:
//   FWorkbenchWorkspace - document template / selection path / commands
//   FWorkbenchApp       - presenter: maps workspace -> live preview instance
//                         + shell widgets; turns widget actions into
//                         workspace mutations
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
struct UIScrollViewport;
struct UIText;
struct UITextField;
struct UIElement;
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

    /// Headless end-to-end smoke (--smoke-actions): drives the real event
    /// path (dispatch + routed-result observation) across new -> palette
    /// add -> rename -> save, then asserts workspace + file state.
    bool bSmokeActions = false;
    /// Document to open at startup (`--yaui=<path>`); applied in buildUI
    /// once the host initialized the virtual file system.
    std::string startupDocumentPath;
    [[nodiscard]] bool getSmokePassed() const { return _bSmokePassed; }

  private:
    void buildToolbar(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildDocumentList(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildCanvas(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildInspector(ya::WidgetTree& tree, ya::UIElement& parent);
    void buildPalette(ya::WidgetTree& tree, ya::UIElement& parent);

    // Document sync (frame boundary, called from updateUI).
    void rebuildPreviewTree();
    void rebuildDocumentRows();
    void syncPresentationState();

    // Commands.
    void cmdNew();
    void cmdOpen();
    void cmdSave();
    void cmdSaveAs();
    void addWidgetFromPalette(const std::string& typeId);
    void deleteSelected();
    void renameSelected(const std::string& name);
    void toggleSelectedVisible();
    void stepSelectedSize(const glm::vec2& delta);
    void setCommandResult(const std::string& text);

    // Preview tree helpers.
    [[nodiscard]] ya::UIElement* findPreviewWidget(const std::string& path);
    [[nodiscard]] ya::UIElement* getSelectedWidget();
    static std::string widgetPathOf(const ya::UIElement& root, const ya::UIElement& widget);

    void handleUnhandledKey(const ya::KeyPressedEvent& keyEvent);
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
    std::shared_ptr<ya::UIButton>      _saveButton;
    std::shared_ptr<ya::UIButton>      _openButton;
    std::shared_ptr<ya::UIButton>      _saveAsButton;
    std::shared_ptr<ya::UITextField>   _pathField;
    std::shared_ptr<ya::UIText>        _statusText;
    std::shared_ptr<ya::UIText>        _commandResultText;

    // Document list.
    std::shared_ptr<ya::UIScrollViewport> _rowScroll;
    std::shared_ptr<ya::UIContainer>      _rowList;
    std::vector<std::shared_ptr<ya::UISelectableRow>> _rows;

    // Preview canvas (live document instance).
    std::shared_ptr<ya::UIPanel>  _canvasPanel;
    std::shared_ptr<ya::UIPanel>  _highlightPanel;
    std::shared_ptr<ya::UIText>   _previewName;
    ya::UIElementRef              _previewRoot;      // instantiated document root
    std::vector<std::shared_ptr<ya::UIElement>> _previewPathIndex; // path-indexed mirror

    // Inspector.
    std::shared_ptr<ya::UITextField> _nameField;
    std::shared_ptr<ya::UIButton>    _visibleToggle;
    std::shared_ptr<ya::UIButton>    _sizeGrow;
    std::shared_ptr<ya::UIButton>    _sizeShrink;
    std::shared_ptr<ya::UIButton>    _deleteButton;
    std::shared_ptr<ya::UIText>      _typeValue;
    std::shared_ptr<ya::UIText>      _sizeValue;

    // Palette.
    std::shared_ptr<ya::UIContainer> _paletteList;

    bool _bRowsDirty   = true;
    bool _bPreviewDirty = true;
};

} // namespace guiworkbench
