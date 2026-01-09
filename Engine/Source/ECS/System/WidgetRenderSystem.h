
#pragma once


#include "Core/System/System.h"
#include "Render/Core/CommandBuffer.h"

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