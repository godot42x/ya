
#pragma once


#include "Foundation/Core/System/System.h"
#include "Foundation/RHI/Core/CommandBuffer.h"

namespace ya
{
struct WidgetRenderSystem : public RenderSystem
{
    virtual void onUpdate(float deltaTime) override {}
    virtual void onRender() override
    {
    }
};
} // namespace ya