
#pragma once

#include "Core/Base.h"
#include <glm/glm.hpp>



namespace ya
{


struct UIElement;

struct UIRenderContext
{
    int layerId = 0; // for layering...
};

struct FUIColor
{
    glm::vec4 data = glm::vec4(1.0f);

    FUIColor() = default;

    FUIColor(float r, float g, float b, float a)
        : data(r, g, b, a)
    {
    }

    [[nodiscard]] const glm::vec4 &asVec4() const { return data; }
};


struct FUIHelper
{
    static bool isPointInRect(const glm::vec2 &point, const glm::vec2 &rectPos, const glm::vec2 &rectSize)
    {
        return point.x >= rectPos.x &&
               point.x <= rectPos.x + rectSize.x &&
               point.y >= rectPos.y &&
               point.y <= rectPos.y + rectSize.y;
    }


    static bool isUIPendingKill(UIElement *el)
    {
        return el == nullptr; // kill flag....
    }
};


struct UILayout
{
    float borderWidth = 1.0f;
};


struct UIAppCtx
{
    glm::vec2 lastMousePos;
};



}; // namespace ya