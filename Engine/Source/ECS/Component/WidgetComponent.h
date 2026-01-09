#pragma once

#include "Core/Base.h"
#include "Core/Reflection/Reflection.h"
#include "Core/UI/UIBase.h"
#include "Core/UI/UIElement.h"
#include "ECS/Component.h"


namespace ya
{
struct WidgetComponent : public IComponent
{
    YA_REFLECT_BEGIN(WidgetComponent)
    YA_REFLECT_END()


  public:
    std::shared_ptr<UIElement> widget;

    WidgetComponent()  = default;
    ~WidgetComponent() = default;
};

} // namespace ya