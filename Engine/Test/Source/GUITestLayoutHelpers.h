#pragma once

#include "GUI/Layout/UILayout.h"

namespace ya
{

// Test fixtures must author geometry on the parent-owned edge even when a
// widget is still detached. These helpers make that edge operation explicit;
// they are not UIElement compatibility methods.
inline void authorSlotPosition(UIElement& widget, const glm::vec2& value)
{
    auto apply = [value](UIElement&, UISlot& edge) {
        if (auto* canvas = edge.as<UICanvasSlot>()) {
            canvas->setOffset(value);
        }
    };
    if (UISlot* edge = widget.getSlot()) {
        apply(widget, *edge);
    }
    else {
        widget.setPendingSlotInitializer(std::move(apply));
    }
}

inline void authorSlotSize(UIElement& widget, const glm::vec2& value)
{
    auto apply = [value](UIElement&, UISlot& edge) {
        if (auto* canvas = edge.as<UICanvasSlot>()) {
            canvas->setFixedSize(value);
            canvas->setWidthSizeMode(EWidgetSizeMode::Fixed);
            canvas->setHeightSizeMode(EWidgetSizeMode::Fixed);
        }
        else if (auto* box = edge.as<UIBoxSlot>()) {
            box->setPreferredSize(value);
        }
        else if (auto* overlay = edge.as<UIOverlaySlot>()) {
            overlay->setPreferredSize(value);
        }
    };
    if (UISlot* edge = widget.getSlot()) {
        apply(widget, *edge);
    }
    else {
        widget.setPendingSlotInitializer(std::move(apply));
    }
}

inline void authorSlotAnchors(UIElement& widget, const glm::vec2& min, const glm::vec2& max)
{
    auto apply = [min, max](UIElement&, UISlot& edge) {
        if (auto* canvas = edge.as<UICanvasSlot>()) {
            canvas->setAnchorMin(min);
            canvas->setAnchorMax(max);
        }
    };
    if (UISlot* edge = widget.getSlot()) {
        apply(widget, *edge);
    }
    else {
        widget.setPendingSlotInitializer(std::move(apply));
    }
}

} // namespace ya
