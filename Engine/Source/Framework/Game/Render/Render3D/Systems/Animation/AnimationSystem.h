#pragma once

#include "Foundation/Core/System/System.h"

namespace ya
{

struct SkeletonAnimationSystem : public ISystem
{
    void onUpdate(float deltaTime) override;
};

} // namespace ya
