#pragma once

#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Stateful composition root, analogous to Slate's SCompoundWidget.
///
/// A compound widget is still an ordinary UIElement in the retained tree. Its
/// internal children are constructed once, from the static DSL, immediately
/// before first attachment. WidgetTree owns lifecycle and frame driving; the
/// compound widget never runs its own loop.
///
/// This type belongs to the native retained widget layer, not the runtime
/// kernel. It is for local composition roots with retained state/lifecycle;
/// reusable cross-cutting interaction stays in UIBehavior, and future
/// document/component adapters may target the same kernel without routing
/// through UICompoundWidget.
struct YA_GUI_API UICompoundWidget : public UIElement
{
    using UIElement::UIElement;

    void prepareForAttach() override;
    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    bool wantsTick() const override { return _bTickEnabled; }

protected:
    virtual void construct() = 0;
    bool _bTickEnabled = false;
    bool _bConstructed = false;

    void enableTick(bool enabled = true) { _bTickEnabled = enabled; }
};

} // namespace ya
