#include "Scene2D/ParticleSystem.h"

#include "Scene/Core/Scene.h"
#include "Scene2D/ParticleEmitterComponent.h"

namespace ya
{

void ParticleSystem::onUpdate(float deltaTime)
{
    if (!_sceneProvider) {
        return;
    }
    Scene* scene = _sceneProvider();
    if (!scene) {
        return;
    }
    // Emission and integration only while the scene is playing; an emitter in
    // a scene being edited must stay exactly as authored.
    if (_tickPolicy && !_tickPolicy()) {
        return;
    }
    scene->getRegistry().view<ParticleEmitterComponent>().each(
        [deltaTime](ParticleEmitterComponent& emitter) { emitter.update(deltaTime); });
}

} // namespace ya
