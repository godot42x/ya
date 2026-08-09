#pragma once

#include "GUI/Widgets/UIElement.h"

#include <functional>

namespace ya
{

/// Button: panel style with hover/pressed states. Click callback is
/// runtime-only (not serialized); hit testing is driven by the tree walker.
struct UIButton : public UIElement
{
    explicit UIButton(std::string name = "Button") : UIElement(std::move(name))
    {
        _hitFilter = EWidgetHitFilter::Stop;
    }

    glm::vec4 _normalColor  = {0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec4 _hoveredColor = {0.6f, 0.6f, 0.6f, 1.0f};
    glm::vec4 _pressedColor = {0.4f, 0.4f, 0.4f, 1.0f};

    // Runtime-only state (not serialized)
    bool                  _bHovered = false;
    bool                  _bPressed = false;
    std::function<void()> _onClick;

    void paintSelf(const WidgetPaintContext& ctx) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void resetHoverState() override { _bHovered = false; }
};

} // namespace ya
