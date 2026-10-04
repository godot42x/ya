#include "Scene2D/Sprite2DComponent.h"

#include "ECS/Entity.h"
#include "Scene2D/SpriteAnimationComponent.h"

namespace ya
{

void Sprite2DComponent::onPostSerialize()
{
    Entity* owner = getOwner();
    if (!owner) {
        return;
    }
    if (SpriteAnimationComponent* animation = owner->tryGetComponent<SpriteAnimationComponent>()) {
        animation->applyDisplayedFrame();
    }
}

bool spriteIsDrawable(const Sprite2DComponent& sprite)
{
    return sprite.bVisible && sprite.image.hasPath() && sprite.image.textureRef.isLoaded();
}

} // namespace ya
