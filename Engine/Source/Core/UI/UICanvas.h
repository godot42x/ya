
#pragma once

#include "UIBase.h"
#include "UIElement.h"


namespace ya
{
struct UICanvas : public UIElement
{
    UI_TYPE(UICanvas, UIElement);

    UICanvas()                 = default;
    UICanvas(const UICanvas &) = default;
};
} // namespace ya