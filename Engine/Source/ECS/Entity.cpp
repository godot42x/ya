#include "Entity.h"
#include "Component.h"
#include "Scene/Scene.h"

namespace ya
{

void Entity::setName(const std::string& newName)
{
    name = newName;
    if (_scene) {
        if (auto* node = _scene->getNodeByEntity(this)) {
            node->setName(newName);
        }
    }
}

Entity::operator bool() const
{
    return _entityHandle != entt::null &&
           _scene &&
           _registry &&
           _scene->isValid() &&
           _scene->isValidEntity(this) &&
           _registry->valid(_entityHandle);
}

} // namespace ya
