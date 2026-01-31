
#pragma once

#include "Core/Common/FWD-std.h"
#include "Core/System/System.h"

namespace ya
{

struct UIComponentSystem : public RenderSystem
{
    UIComponentSystem() {}
    virtual ~UIComponentSystem() = default;

    void onUpdate(float dt) override {}
    void onRender() override;
};

} // namespace ya