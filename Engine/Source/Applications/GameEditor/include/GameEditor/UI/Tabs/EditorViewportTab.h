#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/UI/Viewport/EditorViewportHost.h"

#include <memory>

namespace ya
{

struct UIImage;
struct UIBorder;
struct UIOverlay;

/// Viewport dock tab. The shell talks to this widget only through
/// `IEditorViewportHost`; overlay/gizmo stay on EditorSurface.
class EditorViewportTab : public UICompoundWidget, public IEditorViewportHost
{
  public:
    explicit EditorViewportTab(IEditorViewportHostSink* sink);
    ~EditorViewportTab() override;

    void onAttached() override;
    void onDetached() override;

    void setDisplayImage(const std::shared_ptr<Texture>& texture, bool missing) override;
    void setPreviewImage(const std::shared_ptr<Texture>& texture, const Rect2D& localRect) override;
    [[nodiscard]] Rect2D imageRect() const override;
    [[nodiscard]] bool isWorldPoint(const glm::vec2& logicalPoint) const override;
    [[nodiscard]] bool isHoverable() const override { return true; }
    [[nodiscard]] bool isHovered() const override;
    [[nodiscard]] bool isFocused() const override;
    void takeKeyboardFocus() override;

  protected:
    void construct() override;

  private:
    IEditorViewportHostSink* _sink = nullptr;
    std::shared_ptr<UIOverlay> _stack;
    std::shared_ptr<UIImage> _image;
    std::shared_ptr<UIBorder> _previewFrame;
    std::shared_ptr<UIImage>  _previewImage;
    Rect2D                   _previewLocalRect{};
    bool _bHostRegistered = false;

    /// Resize/relocate the preview panel inside the viewport image.
    void placePreviewPanel(const Rect2D& localRect);
    void clearHostRegistration();
    [[nodiscard]] bool containsTreeNode(const UIElement* node) const;
};

} // namespace ya
