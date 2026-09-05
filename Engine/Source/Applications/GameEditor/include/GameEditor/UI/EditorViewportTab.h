#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/UI/EditorViewportHost.h"

#include <memory>

namespace ya
{

struct UIImage;

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
    [[nodiscard]] Rect2D imageRect() const override;
    [[nodiscard]] bool isHovered() const override;
    [[nodiscard]] bool isFocused() const override;

  protected:
    void construct() override;

  private:
    IEditorViewportHostSink* _sink = nullptr;
    std::shared_ptr<UIImage> _image;
    bool _bHostRegistered = false;

    void clearHostRegistration();
    [[nodiscard]] bool containsTreeNode(const UIElement* node) const;
};

} // namespace ya
