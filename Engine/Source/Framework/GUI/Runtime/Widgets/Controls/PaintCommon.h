#pragma once

// Internal paint helpers shared by the controls (unity build safe: inline).

#include "GUI/Widgets/UIElement.h"

namespace ya
{
namespace paint_util
{

inline glm::vec2 toScreenPxPos(const WidgetPaintContext& ctx, const glm::vec2& point)
{
    return point * ctx.uiScale;
}

inline glm::vec2 toScreenPxSize(const WidgetPaintContext& ctx, const glm::vec2& extent)
{
    return extent * ctx.uiScale;
}

} // namespace paint_util
} // namespace ya
