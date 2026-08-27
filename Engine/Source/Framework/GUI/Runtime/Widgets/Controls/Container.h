#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Layout host for the first formal UIBoxLayout. UIContainer owns visual
/// children and one layout algorithm; box configuration and child intent no
/// longer live as fields on the container/widget itself.
struct YA_GUI_API UIContainer : public UIElement
{
    YA_REFLECT_BEGIN(UIContainer, UIElement)
    YA_REFLECT_END()

    explicit UIContainer(std::string name = "Container", std::string styleKey = {});

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIContainer>; }

    [[nodiscard]] UIBoxLayout&       getBoxLayout() { return _boxLayout; }
    [[nodiscard]] const UIBoxLayout& getBoxLayout() const { return _boxLayout; }
    void                             appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        const auto& l             = _boxLayout;
        node["type"]              = "box";
        node["direction"]         = l.getDirection() == EWidgetBoxLayout::Horizontal ? "horizontal" : "vertical";
        node["spacing"]           = l.getSpacing();
        node["padding"]           = {{"x", l.getPadding().x}, {"y", l.getPadding().y}};
        node["mainAxisAlignment"] = static_cast<int>(l.getMainAxisAlignment());
        node["clipsChildren"]     = l.clipsChildren();
        node["stretchLastChild"]  = l.stretchesLastChild();
    }
    void                           setDirection(EWidgetBoxLayout value) { _boxLayout.setDirection(value); }
    [[nodiscard]] EWidgetBoxLayout getDirection() const { return _boxLayout.getDirection(); }
    void                           setSpacing(float value) { _boxLayout.setSpacing(value); }
    void                           setPadding(glm::vec2 value) { _boxLayout.setPadding(value); }
    void                           setMainAxisAlignment(EWidgetMainAxisAlignment value) { _boxLayout.setMainAxisAlignment(value); }
    void                           setClipChildren(bool value) { _boxLayout.setClipsChildren(value); }
    void                           setStretchLastChild(bool value) { _boxLayout.setStretchLastChild(value); }
    [[nodiscard]] UIBoxSlot*       getBoxSlot(const UIElement& child) const
    {
        return dynamic_cast<UIBoxSlot*>(getSlotForChild(child));
    }

    void                    layout(const Rect2D& parentRect) override;
    void                    layoutAssigned(const Rect2D& rect) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  protected:
    /// Clip the child traversal to this container's rect when clipChildren is
    /// set (GI-302: the base paint owns self rebuild/reuse; this only customizes
    /// the children context).
    void paintChildren(UIFrameBuilder& builder) override;
    /// A changed clip rect invalidates every descendant's resolved clip (GI-304).
    void                                  onLayoutRectChanged() override;
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;

  private:
    UIBoxLayout _boxLayout;
};

} // namespace ya
