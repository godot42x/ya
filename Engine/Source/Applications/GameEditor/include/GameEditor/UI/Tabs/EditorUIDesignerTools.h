#pragma once

#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/Controls/TreeView.h"

#include <memory>
#include <string>

namespace ya
{

class ActionMap;
struct EditorLayer;
struct EditorUIDesignerSession;
class EditorUISlotEdit;
struct IImage;
struct IImageView;
struct Texture;
struct UIComboBox;
struct UIElement;
struct UIImage;
struct UISpinBox;
struct WidgetTree;

class EditorUIHierarchyTab : public UICompoundWidget
{
  public:
    /// `actions` is the UI page root's map: the context menu runs the same
    /// Duplicate / Delete the shortcuts do.
    EditorUIHierarchyTab(EditorLayer& layer, ActionMap* actions);
    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer   = nullptr;
    ActionMap*   _actions = nullptr;
    std::shared_ptr<ReactiveList<UITreeView::FNode>> _roots;
    std::shared_ptr<Reactive<std::string>> _selection;
    std::shared_ptr<UITreeView> _treeView;
    std::string _treeFingerprint;
    std::string _selectionFingerprint;

    void refresh();
    void openContextMenu(const std::string& nodeId, const glm::vec2& logicalPoint);
};

class EditorUIInspectorTab : public UICompoundWidget
{
  public:
    explicit EditorUIInspectorTab(EditorLayer& layer);
    ~EditorUIInspectorTab() override;
    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    std::shared_ptr<struct UIContainer> _inspectorHost;
    std::shared_ptr<class EditorAutoPropertySection> _inspectorSection;
    /// The selected widget's parent-owned slot: its own group below the
    /// widget's properties, edited through `_slotEdit`'s args copy.
    UIElementRef _slotGroup;
    std::shared_ptr<class EditorAutoPropertySection> _slotSection;
    std::unique_ptr<EditorUISlotEdit> _slotEdit;
    std::string _inspectorFingerprint;

    void refresh();
    void rebuildInspector(WidgetTree& tree, UIElement* selected);
    void clearInspector(WidgetTree& tree);
};

class EditorUIPaletteTab : public UICompoundWidget
{
  public:
    explicit EditorUIPaletteTab(EditorLayer& layer);

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
};

/// The UI Designer's canvas: shows the picture EditorUICanvasCompositor records
/// for the open document and turns pointer input on it into designer edits
/// (select, move, resize, pan, zoom). The gesture is this widget's own
/// WidgetTree input; the edit itself goes through EditorUIDesignerSession.
class EditorUICanvasTab : public UICompoundWidget
{
    EditorUIDesignerSession*    _designer = nullptr;
    std::shared_ptr<UIImage>    _image;
    /// Design resolution toolbar; follows the session when it changes elsewhere.
    std::shared_ptr<UIComboBox> _resolutionPresets;
    std::shared_ptr<UISpinBox>  _resolutionWidth;
    std::shared_ptr<UISpinBox>  _resolutionHeight;
    glm::uvec2                  _shownResolution = {0, 0};
    /// Chrome wrap of the canvas picture, rebuilt only when the picture's
    /// image or view changes (a resize), not every frame.
    std::shared_ptr<Texture>    _texture;
    std::shared_ptr<IImage>     _textureImage;
    std::shared_ptr<IImageView> _textureView;

    // Pointer gesture in flight. Left press selects and may start a move or a
    // resize session; right/middle press pans.
    UIElement* _pressHit   = nullptr;
    glm::vec2  _pressPoint = {0.0f, 0.0f}; // canvas logical px at the press
    bool       _bPressing  = false;
    bool       _bPanning   = false;
    glm::vec2  _panLast    = {0.0f, 0.0f}; // view px of the last pan step

  public:
    explicit EditorUICanvasTab(EditorLayer& layer);

    void onAttached() override;
    void onDetached() override;
    void tick(float deltaSeconds) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override;

  protected:
    void construct() override;

  private:
    [[nodiscard]] glm::vec2 toView(const glm::vec2& logicalPoint) const;
    void beginPress(const glm::vec2& viewPoint);
    void endGesture();
    void pushPicture();
    void syncResolution();
};

} // namespace ya
