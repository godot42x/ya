#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace ya
{

inline constexpr float kDockHideTabBarSize = 12.0f;
/// Keep 1px of dock/floating content off the chrome clip so 1px control
/// outlines at the panel corners are not eaten by `clipChildren`.
inline constexpr float kDockContentInset = 1.0f;

/// UE-style reveal control: a right triangle whose right angle sits on the
/// content's top-left. It is only mounted while the tab strip is hidden;
/// click restores the strip. Shared by docked stacks and floating windows
/// so unity builds of those two TUs do not collide on file-local copies.
struct FDockHideTabBarAffordance final : UIElement
{
    std::function<void()> _onReveal;
    VisualFlag            _bHovered;

    FDockHideTabBarAffordance(std::string name, std::function<void()> onReveal)
        : UIElement(std::move(name))
        , _onReveal(std::move(onReveal))
        , _bHovered(*this)
    {
        _hitFilter   = EWidgetHitFilter::Stop;
        _zOrder      = 8;
        _visibility  = EWidgetVisibility::Collapsed;
    }

    [[nodiscard]] bool containsRevealTriangle(const glm::vec2& logicalPoint) const
    {
        const glm::vec2 local = logicalPoint - _layoutRect.pos;
        const float     size  = std::min(_layoutRect.extent.x, _layoutRect.extent.y);
        return local.x >= 0.0f && local.y >= 0.0f && local.x + local.y <= size;
    }

    [[nodiscard]] bool hitTestSelf(const glm::vec2& logicalPoint) const override
    {
        return isHitTestableSelf() && containsRevealTriangle(logicalPoint);
    }

    [[nodiscard]] bool isHoverable() const override { return true; }
    void onPointerEnter() override { _bHovered = true; }
    void onPointerLeave() override { _bHovered = false; }

    void paintSelf(UIFrameBuilder& builder) override
    {
        const glm::vec2 origin = _layoutRect.pos;
        const float     box    = std::min(_layoutRect.extent.x, _layoutRect.extent.y);
        const float     size   = _bHovered ? box : std::max(1.0f, box * 0.78f);
        const glm::vec4 fill   = _bHovered ? glm::vec4{0.92f, 0.95f, 1.00f, 0.92f}
                                           : glm::vec4{0.78f, 0.82f, 0.90f, 0.40f};
        const int rows = std::max(1, static_cast<int>(std::ceil(size)));
        for (int i = 0; i < rows; ++i) {
            const float y     = static_cast<float>(i);
            const float width = size - y;
            if (width <= 0.0f) {
                break;
            }
            builder.addRoundedRect(
                Rect2D{
                    .pos    = {origin.x, origin.y + y},
                    .extent = {width, 1.0f},
                },
                fill,
                0.0f);
        }
        if (_bHovered) {
            builder.addLine(origin + glm::vec2{size, 0.0f},
                            origin + glm::vec2{0.0f, size},
                            glm::vec4{1.0f, 1.0f, 1.0f, 0.88f},
                            1.0f);
        }
    }

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        const bool inside = containsRevealTriangle(ctx.logicalPoint);
        if (event.getEventType() == EEvent::MouseMoved) {
            _bHovered = inside;
            return inside;
        }
        if (event.getEventType() == EEvent::MouseButtonPressed && inside) {
            if (_onReveal) {
                _onReveal();
            }
            return true;
        }
        return false;
    }

    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override { _bHovered = false; }

    [[nodiscard]] glm::vec2 computeDesiredSize() const override
    {
        return {kDockHideTabBarSize, kDockHideTabBarSize};
    }
};

} // namespace ya
