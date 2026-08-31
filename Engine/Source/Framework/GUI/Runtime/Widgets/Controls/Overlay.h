#pragma once

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Stacked layout host (UMG Overlay / Godot Control children). Each child is
/// independently aligned in the parent rect via UIOverlaySlot; later children
/// paint on top. This is not UIPopupOverlay (popup shield / modal).
struct YA_GUI_API UIOverlay : public UIElement
{
    YA_REFLECT_BEGIN(UIOverlay, UIElement)
    YA_REFLECT_END()

    explicit UIOverlay(std::string name = "Overlay");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIOverlay>; }

    [[nodiscard]] UIOverlayLayout& getOverlayLayout() { return _overlayLayout; }
    [[nodiscard]] const UIOverlayLayout& getOverlayLayout() const { return _overlayLayout; }
    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override { node["type"] = "overlay"; }
    [[nodiscard]] UIOverlaySlot* getOverlaySlot(const UIElement& child) const
    {
        if (UISlot* edge = getSlotForChild(child)) {
            return edge->as<UIOverlaySlot>();
        }
        return nullptr;
    }

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  protected:
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;

  private:
    UIOverlayLayout _overlayLayout;
};

} // namespace ya
