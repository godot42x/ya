#include "Render3D/Common/ParticleExtraction.h"

#include "Render3D/RenderFrameData.h"

#include <algorithm>

namespace ya
{

void appendParticleCandidates(const ParticleExtractionInput& in, std::vector<WorldSpriteCandidate>& out)
{
    if (!in.emitter || !in.atlas || !in.atlas->isValid()) {
        return;
    }
    const ParticleEmitterComponent& emitter = *in.emitter;
    if (emitter._particles.empty()) {
        return;
    }
    const glm::vec3 right  = glm::vec3(in.world[0]);
    const glm::vec3 up     = glm::vec3(in.world[1]);
    const glm::vec3 origin = glm::vec3(in.world[3]);

    for (const Particle& p : emitter._particles) {
        const float k = p.lifetime > 0.0f ? std::clamp(p.age / p.lifetime, 0.0f, 1.0f) : 1.0f;
        const float size = p.sizeStart + (p.sizeEnd - p.sizeStart) * k;
        if (size <= 0.0f) {
            continue;
        }
        const glm::vec4 color = p.colorStart + (p.colorEnd - p.colorStart) * k;
        if (color.a < 0.01f) {
            continue;
        }

        WorldSpriteCandidate candidate{};
        candidate.worldCenter = origin + right * p.position.x + up * p.position.y;
        candidate.axisX       = right * size;
        candidate.axisY       = up * size;
        candidate.uvRect      = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        candidate.tint        = color;
        candidate.texture.setTexture(in.atlas->texture);
        candidate.texture.setSampler(in.atlas->sampler);
        candidate.sortPointY = candidate.worldCenter.y;
        candidate.drawKey    = makeSpriteDrawKey(emitter.layer, emitter.bYSort, candidate.sortPointY,
                                                 emitter.sortOrder, in.entityId, 0);
        out.push_back(std::move(candidate));
    }
}

} // namespace ya
