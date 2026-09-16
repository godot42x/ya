#pragma once

#include "GUI/Layout/UIContentLayout.h"
#include "GUI/Widgets/UIElement.h"

#include <limits>

namespace ya
{

/// Single-child constraint host (UMG SizeBox / Godot MarginContainer).
/// Padding insets the child; optional width/height overrides replace that
/// axis of desired size; min/max clamp the result. The assigned rect still
/// fills whatever the parent gave this widget (Fill in a box, stretch
/// anchors, etc.).
struct YA_GUI_API UISizeBox : public UIElement
{
    using SlotArgs = FContentSlotArgs;

    YA_REFLECT_BEGIN(UISizeBox, UIElement)
    YA_REFLECT_END()

    explicit UISizeBox(std::string name = "SizeBox");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISizeBox>; }

    [[nodiscard]] UISingleChildLayout& getContentLayout() { return _contentLayout; }
    [[nodiscard]] const UISingleChildLayout& getContentLayout() const { return _contentLayout; }
    void setPadding(FMargin value) { _contentLayout.setPadding(value); }
    void setPadding(glm::vec2 value) { _contentLayout.setPadding(value); }
    [[nodiscard]] const FMargin& getPadding() const { return _contentLayout.getPadding(); }
    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override { node["type"] = "sizeBox"; node["padding"] = {{"left", getPadding().left}, {"top", getPadding().top}, {"right", getPadding().right}, {"bottom", getPadding().bottom}}; node["widthOverride"] = getWidthOverride(); node["heightOverride"] = getHeightOverride(); }

    /// Negative means "do not override" (child + padding decide the axis).
    void setWidthOverride(float value);
    void setHeightOverride(float value);
    void setMinSize(glm::vec2 value);
    void setMaxSize(glm::vec2 value);
    [[nodiscard]] float getWidthOverride() const { return _widthOverride; }
    [[nodiscard]] float getHeightOverride() const { return _heightOverride; }
    [[nodiscard]] const glm::vec2& getMinSize() const { return _minSize; }
    [[nodiscard]] const glm::vec2& getMaxSize() const { return _maxSize; }

    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    /// The size box owns both axes, so child intent is carried by a
    /// single-child slot (Fill by default, or align at desired size).
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;

  private:
    UISingleChildLayout _contentLayout;
    float               _widthOverride  = -1.0f;
    float               _heightOverride = -1.0f;
    glm::vec2           _minSize        = {0.0f, 0.0f};
    glm::vec2           _maxSize        = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
};

} // namespace ya
