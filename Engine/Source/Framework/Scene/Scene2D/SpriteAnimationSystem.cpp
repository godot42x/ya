#include "Scene2D/SpriteAnimationSystem.h"

#include "Scene/Core/Scene.h"
#include "Scene2D/SpriteAnimationComponent.h"

namespace ya
{

void SpriteAnimationSystem::onUpdate(float deltaTime)
{
    if (!_sceneProvider || (_tickPolicy && !_tickPolicy())) {
        return;
    }
    Scene* scene = _sceneProvider();
    if (!scene) {
        return;
    }
    scene->getRegistry().view<SpriteAnimationComponent>().each(
        [deltaTime](SpriteAnimationComponent& animation) { animation.advance(deltaTime); });
}

} // namespace ya
