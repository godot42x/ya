#pragma once

#include "Scene2D/ParticleEmitterComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace ya
{

struct WorldSpriteCandidate;
struct TextureBinding;

/// Everything appendParticleCandidates needs besides the component itself.
/// `atlas` is the resolved particle texture; a null binding or a non-drawable
/// emitter yields no candidates (same "no substitute image" rule as sprites).
struct ParticleExtractionInput
{
    const ParticleEmitterComponent* emitter   = nullptr;
    glm::mat4                       world     = glm::mat4(1.0f);
    uint32_t                        entityId  = 0;
    const TextureBinding*           atlas     = nullptr;
};

/// Expands the emitter's live particles into sprite candidates, one quad per
/// particle. Positions are component-local (the emitter's own 2D offset), so
/// they ride the entity's world matrix like a sprite's authored quad. Appends
/// to `out`.
YA_RENDER_3D_API void appendParticleCandidates(const ParticleExtractionInput&     in,
                                               std::vector<WorldSpriteCandidate>& out);

} // namespace ya
