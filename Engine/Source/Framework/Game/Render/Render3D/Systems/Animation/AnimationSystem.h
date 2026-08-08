#pragma once

#include "Core/System/System.h"

namespace ya
{

struct SkeletonAnimationSystem : public ISystem
{
    void onUpdate(float deltaTime) override;
};

} // namespace ya
