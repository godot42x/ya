#include "Scene2D/Sprite2DComponent.h"

namespace ya
{

bool spriteIsDrawable(const Sprite2DComponent& sprite)
{
    return sprite.bVisible && sprite.image.hasPath() && sprite.image.textureRef.isLoaded();
}

} // namespace ya
