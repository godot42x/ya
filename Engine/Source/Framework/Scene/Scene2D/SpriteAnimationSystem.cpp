#include "Scene2D/SpriteAnimationSystem.h"

#include "Scene/Core/Scene.h"
#include "Scene2D/SpriteAnimationComponent.h"

namespace ya
{

void SpriteAnimationSystem::onUpdate(float deltaTime)
{
    if (!_sceneProvider) {
        return;
    }
    Scene* scene = _sceneProvider();
    if (!scene) {
        return;
    }
    // Playback advances only while the policy allows. The display still catches
    // up when an animation-set slot is replaced, including in the editor,
    // where the policy is false and the playhead must not move.
    const bool bAdvance = !_tickPolicy || _tickPolicy();
    scene->getRegistry().view<SpriteAnimationComponent>().each(
        [deltaTime, bAdvance](SpriteAnimationComponent& animation) {
            if (bAdvance) {
                animation.advance(deltaTime);
            }
            animation.refreshDisplayedFrame();
        });
}

} // namespace ya
