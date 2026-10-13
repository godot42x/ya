#include "Scene2D/ParticleEmitterComponent.h"

#include <algorithm>
#include <cmath>

namespace ya
{

namespace
{

// xorshift32: deterministic, tiny, and good enough for a 2D effect. A fixed
// seed + fixed dt makes a burst reproducible, which the extraction test relies
// on.
float nextUnit(uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

float nextSigned(uint32_t& state)
{
    return nextUnit(state) * 2.0f - 1.0f;
}

} // namespace

void ParticleEmitterComponent::spawnBurst(int32_t count, float overLifetime)
{
    if (count <= 0) {
        return;
    }
    for (int32_t i = 0; i < count; ++i) {
        Particle p;
        p.age      = 0.0f;
        p.lifetime = std::max(0.01f, overLifetime > 0.0f ? overLifetime : lifetime);
        p.sizeStart = sizeStart;
        p.sizeEnd   = sizeEnd;
        p.colorStart = colorStart;
        p.colorEnd   = colorEnd;

        // Rotate the base velocity by a random angle inside the spread cone.
        const float angle = spread > 0.0f ? nextSigned(_rngState) * spread : 0.0f;
        const float s     = std::sin(angle);
        const float c     = std::cos(angle);
        p.velocity        = glm::vec2(velocity.x * c - velocity.y * s,
                                      velocity.x * s + velocity.y * c);
        // A little positional jitter so a burst does not look like one line.
        p.position = glm::vec2(nextSigned(_rngState) * 0.05f, nextSigned(_rngState) * 0.05f);
        _particles.push_back(p);
    }
}

void ParticleEmitterComponent::update(float dt)
{
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    // Continuous emission: accumulate a fractional carry so a low rate emits
    // evenly instead of one-per-frame bursts.
    if (bEmitting && rate > 0.0f) {
        _emitCarry += rate * dt;
        const int whole = static_cast<int>(_emitCarry);
        if (whole > 0) {
            _emitCarry -= static_cast<float>(whole);
            spawnBurst(whole);
        }
    }

    for (Particle& p : _particles) {
        p.age += dt;
        p.velocity += gravity * dt;
        p.position += p.velocity * dt;
    }
    std::erase_if(_particles, [](const Particle& p) { return p.age >= p.lifetime; });
}

} // namespace ya
