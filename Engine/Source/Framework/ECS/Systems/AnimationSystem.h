#pragma once

#include "Core/Api.h"
#include "Core/System/System.h"

#include <functional>

namespace ya
{

struct Scene;

struct YA_GAMEPLAY_SYSTEMS_API SkeletonAnimationSystem : public ISystem
{
    using SceneProvider = std::function<Scene*()>;
    using TickPolicy    = std::function<bool()>;

    /// Injected seams (bound by the Host at startup; no App access from here).
    void setSceneProvider(SceneProvider provider);
    void setTickPolicy(TickPolicy policy);

    void onUpdate(float deltaTime) override;

  private:
    SceneProvider _sceneProvider;
    TickPolicy    _tickPolicy;
};

} // namespace ya
