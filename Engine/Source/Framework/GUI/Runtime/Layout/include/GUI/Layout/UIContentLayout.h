#pragma once

#include "Core/Api.h"
#include "GUI/Layout/UILayoutBase.h"
#include "GUI/Layout/UILayoutTypes.h"

#include <glm/glm.hpp>

#include <memory>

namespace ya
{

/// Layout for a single content child that fills an inset content rect.
/// Buttons, SizeBox, scroll, split panes and compound widgets reuse this
/// instead of each reimplementing "parent rect minus padding".
class YA_GUI_API UISingleChildLayout final : public UILayout
{
public:
    [[nodiscard]] const FMargin& getPadding() const { return _padding; }
    void setPadding(FMargin value);
    void setPadding(glm::vec2 value) { setPadding(FMargin::hv(value)); }

    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

private:
    FMargin _padding{};
};

/// Content-region slot: one child inside a parent-owned rect (button label,
/// scroll content, split pane, size box, compound root). Same geometric
/// fields as overlay, but a distinct type so overlay stacking cannot be
/// confused with a single content host.
class YA_GUI_API UIContentSlot final : public UISlot
{
public:
    UIContentSlot(UIElement& parent, UIElement& child);

    [[nodiscard]] EUIOverlayAlignment getHAlign() const { return _hAlign; }
    [[nodiscard]] EUIOverlayAlignment getVAlign() const { return _vAlign; }
    [[nodiscard]] const FMargin& getPadding() const { return _padding; }
    [[nodiscard]] const glm::vec2& getPreferredSize() const { return _preferredSize; }

    void setHAlign(EUIOverlayAlignment value);
    void setVAlign(EUIOverlayAlignment value);
    void setPadding(FMargin value);
    void setPreferredSize(glm::vec2 value);
    void setPadding(glm::vec2 value) { setPadding(FMargin::hv(value)); }
    void apply(const struct FContentSlotArgs& args);
    /// Exact slot state as args: `assign(toArgs())` is the identity. `apply`
    /// is the construct-time form and treats a zero size as "unset".
    [[nodiscard]] struct FContentSlotArgs toArgs() const;
    void assign(const struct FContentSlotArgs& args);
    void appendRuntimeDiagnostics(nlohmann::json& node) const override;
    void serialize(nlohmann::json& node) const override;
    void deserialize(const nlohmann::json& node) override;
    [[nodiscard]] bool isAutoSizeActive() const override;

private:
    EUIOverlayAlignment _hAlign  = EUIOverlayAlignment::Fill;
    EUIOverlayAlignment _vAlign  = EUIOverlayAlignment::Fill;
    FMargin             _padding{};
    glm::vec2           _preferredSize = {0.0f, 0.0f};
};

struct FContentSlotArgs
{
    using SlotType = UIContentSlot;

    EUIOverlayAlignment hAlign  = EUIOverlayAlignment::Fill;
    EUIOverlayAlignment vAlign  = EUIOverlayAlignment::Fill;
    FMargin             padding = {};
    /// Non-zero on an axis overrides the child's desired size for that axis.
    glm::vec2           preferredSize = {0.0f, 0.0f};
};

} // namespace ya
