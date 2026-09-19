#pragma once

#include "GUI/Layout/UIContentLayout.h"
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
/// The first constructed child is the composition host and fills this
/// widget through a parent-owned UIContentSlot. The compound does not
/// hand-assign that rect.
///
/// This type belongs to the native retained widget layer, not the runtime
/// kernel. It is for local composition roots with retained state/lifecycle;
/// reusable cross-cutting interaction stays in UIBehavior, and future
/// document/component adapters may target the same kernel without routing
/// through UICompoundWidget.
///
/// Frame lifecycle is UIElement's, not this type's: a compound with per-frame
/// state calls the inherited enableTick() and overrides tick(), while
/// behaviours attached to it are ticked through the same wantsTick(). This
/// type used to answer wantsTick() from its own flag only, which silently
/// dropped every behaviour (tween included) attached to a compound.
struct YA_GUI_API UICompoundWidget : public UIElement
{
    using SlotArgs = FContentSlotArgs;

    explicit UICompoundWidget(std::string name = "Widget", std::string styleKey = {})
        : UIElement(std::move(name), std::move(styleKey))
    {
        bindHostLayout(_contentLayout);
    }

    void prepareForAttach() override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
    [[nodiscard]] std::unique_ptr<UISlot> createSlotForChild(UIElement& child) override;
    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        node["type"] = "singleChild";
    }

  protected:
    virtual void construct() = 0;
    bool _bConstructed = false;

  private:
    UISingleChildLayout _contentLayout;
};

} // namespace ya
