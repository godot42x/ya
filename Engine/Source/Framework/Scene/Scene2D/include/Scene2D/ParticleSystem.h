#pragma once

#include "Core/Api.h"
#include "Core/System/System.h"

#include <functional>

namespace ya
{

struct Scene;

/// Advances every ParticleEmitterComponent of the active Scene: emission,
/// integration and retirement. Registered by the Host in the Simulation group,
/// so game pause stops it with the scripts.
///
/// Live particles are simulation state, not serialized data, so this system
/// only runs against a scene that is playing (the same tick policy the sprite
/// animation system uses). The frame extractor reads the pool afterwards and
/// turns each particle into a sprite candidate; there is no particle pass.
struct YA_SCENE_2D_API ParticleSystem : public ISystem
{
    using SceneProvider = std::function<Scene*()>;
    using TickPolicy    = std::function<bool()>;

    SceneProvider _sceneProvider;
    /// False while authoring: an emitter must not accumulate particles in a
    /// scene the user is editing.
    TickPolicy _tickPolicy;

    void onUpdate(float deltaTime) override;
};

} // namespace ya
