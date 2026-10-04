#pragma once

#include "Core/System/System.h"
#include "Core/Api.h"

#include <functional>

namespace ya
{

struct Scene;

/// Advances every SpriteAnimationComponent of the active Scene. Registered by
/// the Host in the Simulation group, so game pause stops it with the scripts.
struct YA_SCENE_2D_API SpriteAnimationSystem : public ISystem
{
    using SceneProvider = std::function<Scene*()>;
    using TickPolicy    = std::function<bool()>;

    /// Injected seams (bound by the Host at startup; no App access from here).
    SceneProvider _sceneProvider;
    /// False while nothing is playing (editor authoring): animation writes
    /// `Sprite2DComponent::uvRect`, which is serialized, so it must not run
    /// against a scene the user is editing.
    TickPolicy _tickPolicy;

    void onUpdate(float deltaTime) override;
};

} // namespace ya
