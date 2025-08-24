#include "Entity.h"
#include "Component.h"
#include "Scene.h"


namespace ya
{
Entity::Entity(entt::entity handle, Scene *scene)
    : _entityHandle(handle), _scene(scene)
{
}

bool Entity::isValid() const
{
    return _scene && _scene->isValidEntity(*this);
}

} // namespace ya
