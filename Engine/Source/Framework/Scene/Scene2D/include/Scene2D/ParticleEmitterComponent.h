#pragma once

#include "Core/Common/TextureSlot.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ya
{

/// One live particle. Simulation state, not authoring data: the component owns
/// the pool and the frame extractor reads it to make a sprite candidate per
/// particle.
struct Particle
{
    glm::vec2 position{0.0f};
    glm::vec2 velocity{0.0f};
    float     age      = 0.0f;
    float     lifetime = 1.0f;
    float     sizeStart = 0.2f;
    float     sizeEnd   = 0.0f;
    glm::vec4 colorStart{1.0f};
    glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
};

/**
 * @brief A 2D particle emitter on a Node3D, next to a TransformComponent.
 *
 * CPU simulation: `update()` (driven by ParticleSystem) advances the pool, and
 * the frame extractor expands each live particle into a world sprite candidate
 * on the existing sprite pass -- no separate particle pass and no GPU buffer to
 * keep alive across a frame. A burst (`spawnBurst`) covers one-shot effects
 * (a hit spark), a positive `rate` covers a continuous effect (a torch).
 *
 * The texture is a plain TextureSlot, the same asset form as a sprite, so the
 * same sampler rules (pixel art wants Nearest) apply for free.
 */
struct YA_SCENE_2D_API ParticleEmitterComponent : public IComponent
{
    YA_REFLECT_BEGIN(ParticleEmitterComponent, IComponent)
    YA_REFLECT_FIELD(image)
    YA_REFLECT_FIELD(bEmitting)
    YA_REFLECT_FIELD(rate)            ///< Particles per second while emitting
    YA_REFLECT_FIELD(lifetime)        ///< Seconds a particle lives
    YA_REFLECT_FIELD(velocity)        ///< Base velocity, world units per second
    YA_REFLECT_FIELD(spread)          ///< Random cone half-angle, radians
    YA_REFLECT_FIELD(gravity)         ///< Acceleration, world units per second^2
    YA_REFLECT_FIELD(sizeStart, .manipulate(0.0f, 4.0f, 0.01f, ya::reflection::ManipulatorType::Slider))
    YA_REFLECT_FIELD(sizeEnd, .manipulate(0.0f, 4.0f, 0.01f, ya::reflection::ManipulatorType::Slider))
    YA_REFLECT_FIELD(colorStart, .color())
    YA_REFLECT_FIELD(colorEnd, .color())
    YA_REFLECT_FIELD(layer)
    YA_REFLECT_FIELD(sortOrder)
    YA_REFLECT_FIELD(bYSort)
    YA_REFLECT_END()

    /// Particle sheet. Empty leaves every particle undrawn (same "no substitute
    /// image" rule as a sprite).
    TextureSlot image;

    /// Emit continuously? `spawnBurst` fires regardless.
    bool  bEmitting = true;
    float rate      = 0.0f;      ///< Particles per second while emitting; 0 = burst only
    float lifetime  = 1.0f;

    glm::vec2 velocity{0.0f, 1.0f};
    /// Random cone half-angle applied to `velocity`, in radians. 0 = no spread.
    float     spread  = 0.0f;
    glm::vec2 gravity{0.0f, -2.0f};

    float     sizeStart  = 0.2f;
    float     sizeEnd    = 0.0f;
    glm::vec4 colorStart{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};

    int32_t layer     = 0;
    int32_t sortOrder = 50;  ///< Over most sprites by default
    bool    bYSort    = false;

    /// Live particles (simulation state, not serialized).
    std::vector<Particle> _particles;
    /// Fractional emission carry, so a non-integer rate still emits evenly.
    float _emitCarry = 0.0f;
    /// Random generator seed; kept on the component so a scene reload is stable.
    uint32_t _rngState = 0x9E3779B9u;

    /// Advance the pool by `dt` seconds: emit, integrate, retire.
    void update(float dt);
    /// Spawn `count` particles now, ignoring the rate (a hit spark, a puff).
    void spawnBurst(int32_t count, float overLifetime = 0.0f);
    /// Set a seed so a burst is reproducible (tests).
    void setSeed(uint32_t seed) { _rngState = seed == 0 ? 0x9E3779B9u : seed; }
};

} // namespace ya
