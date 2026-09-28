#pragma once

#include "Core/Api.h"
#include "GUI/Layout/UILayoutBase.h"
#include "GUI/Layout/UILayoutTypes.h"

#include <glm/glm.hpp>

#include <memory>

namespace ya
{

/// Overlay slot: one child independently aligned inside the parent rect
/// (UMG Overlay / stacked Control). Fill stretches that axis; otherwise the
/// child keeps its desired size and Start/Center/End place it.
class YA_GUI_API UIOverlaySlot final : public UISlot
{
public:
    UIOverlaySlot(UIElement& parent, UIElement& child);

    [[nodiscard]] EUIOverlayAlignment getHAlign() const { return _hAlign; }
    [[nodiscard]] EUIOverlayAlignment getVAlign() const { return _vAlign; }
    [[nodiscard]] const FMargin& getPadding() const { return _padding; }
    [[nodiscard]] const glm::vec2& getPreferredSize() const { return _preferredSize; }

    void setHAlign(EUIOverlayAlignment value);
    void setVAlign(EUIOverlayAlignment value);
    void setPadding(FMargin value);
    void setPreferredSize(glm::vec2 value);
    void setPadding(glm::vec2 value) { setPadding(FMargin::hv(value)); }
    void apply(const struct FOverlaySlotArgs& args);
    /// Exact slot state as args: `assign(toArgs())` is the identity. `apply`
    /// is the construct-time form and treats a zero size as "unset".
    [[nodiscard]] struct FOverlaySlotArgs toArgs() const;
    void assign(const struct FOverlaySlotArgs& args);
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

struct FOverlaySlotArgs
{
    using SlotType = UIOverlaySlot;

    EUIOverlayAlignment hAlign  = EUIOverlayAlignment::Fill;
    EUIOverlayAlignment vAlign  = EUIOverlayAlignment::Fill;
    FMargin             padding = {};
    /// Non-zero on an axis overrides the child's desired size for that axis.
    glm::vec2           preferredSize = {0.0f, 0.0f};
};

/// Stacked children sharing one parent rect. Each child is arranged through
/// its UIOverlaySlot; child canvas anchors are ignored (layoutAssigned).
class YA_GUI_API UIOverlayLayout final : public UILayout
{
public:
    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;
};

} // namespace ya
