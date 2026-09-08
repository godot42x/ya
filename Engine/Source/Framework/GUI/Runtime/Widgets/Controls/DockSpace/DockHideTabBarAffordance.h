#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <algorithm>
#include <functional>

namespace ya
{

inline constexpr float kDockHideTabBarSize = 12.0f;
/// Keep 1px of dock/floating content off the chrome clip so 1px control
/// outlines at the panel corners are not eaten by `clipChildren`.
inline constexpr float kDockContentInset = 1.0f;

/// Shared hide/show tab-bar affordance for docked leaves and floating windows.
/// One type so unity builds of DockSpace.cpp + DockFloatingWindow.cpp do not
/// collide on file-local anonymous-namespace copies.
struct FDockHideTabBarAffordance final : UIElement
{
    FDockHideTabBarAffordance(std::string name,
                              std::function<void()> onToggle,
                              std::function<bool()> isFolded)
        : UIElement(std::move(name))
        , _onToggle(std::move(onToggle))
        , _isFolded(std::move(isFolded))
        , _bHovered(*this)
    {
        _hitFilter = EWidgetHitFilter::Stop;
        _zOrder    = 8;
    }

    [[nodiscard]] bool hitTestSelf(const glm::vec2& logicalPoint) const override
    {
        return isHitTestableSelf() && hitTestLayoutRect(logicalPoint);
    }

    [[nodiscard]] bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }

    void paintSelf(UIFrameBuilder& builder) override
    {
        const glm::vec4 color = _bHovered ? glm::vec4{0.78f, 0.82f, 0.90f, 1.0f}
                                          : glm::vec4{0.52f, 0.56f, 0.64f, 0.95f};
        const glm::vec2 p = _layoutRect.pos;
        const float     s = std::min(_layoutRect.extent.x, _layoutRect.extent.y);
        const float     inset = std::max(1.0f, s * 0.18f);
        if (_isFolded && _isFolded()) {
            const glm::vec2 a = {p.x + inset, p.y + inset};
            const glm::vec2 b = {p.x + inset, p.y + s - inset};
            const glm::vec2 c = {p.x + s - inset, p.y + s * 0.5f};
            builder.addLine(a, b, color, 1.5f);
            builder.addLine(b, c, color, 1.5f);
            builder.addLine(c, a, color, 1.5f);
            return;
        }
        const glm::vec2 a = {p.x + inset, p.y + inset};
        const glm::vec2 b = {p.x + s - inset, p.y + inset};
        const glm::vec2 c = {p.x + s * 0.5f, p.y + s - inset};
        builder.addLine(a, b, color, 1.5f);
        builder.addLine(b, c, color, 1.5f);
        builder.addLine(c, a, color, 1.5f);
    }

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        const bool inside = hitTestLayoutRect(ctx.logicalPoint);
        if (event.getEventType() == EEvent::MouseMoved) {
            _bHovered = inside;
            return inside;
        }
        if (event.getEventType() == EEvent::MouseButtonPressed && inside) {
            if (_onToggle) {
                _onToggle();
            }
            return true;
        }
        return false;
    }

    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override { _bHovered = false; }

private:
    std::function<void()> _onToggle;
    std::function<bool()> _isFolded;
    VisualFlag            _bHovered;
};

} // namespace ya
