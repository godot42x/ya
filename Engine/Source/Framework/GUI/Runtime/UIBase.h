#pragma once

#include "Foundation/Core/Base.h"
#include "Foundation/Core/Common/Types.h"
#include <glm/glm.hpp>

namespace ya
{

/// Canvas transform applied to UI layout space (editor 2D canvas pan/zoom).
/// The runtime uses identity (pan = 0, zoom = 1).
struct UICanvasTransform
{
    glm::vec2 pan  = {0.0f, 0.0f};
    float     zoom = 1.0f;
};

/// Paint context passed down the Node2D tree during the paint pass.
/// `uiScale` maps logical viewport pixels to render-target pixels.
struct UIPaintContext
{
    glm::vec2         uiScale = {1.0f, 1.0f};
    UICanvasTransform canvas;
};

/// Event context for Node2D hit-testing / event dispatch.
/// `canvasPoint` is in canvas logical space (top-left origin, Y down).
struct UIEventContext
{
    glm::vec2 canvasPoint = {0.0f, 0.0f};
};

struct UIAppCtx
{
    glm::vec2 lastMousePos;
    bool      bInViewport = false;
    Rect2D    viewportRect;
};

struct FUIHelper
{
    static bool isPointInRect(const glm::vec2& point, const glm::vec2& rectPos, const glm::vec2& rectSize)
    {
        return point.x >= rectPos.x &&
               point.x <= rectPos.x + rectSize.x &&
               point.y >= rectPos.y &&
               point.y <= rectPos.y + rectSize.y;
    }
};

} // namespace ya
