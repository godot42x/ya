#include "Entity.h"
#include "Component.h"

#include "Runtime/App/App.h"
#include "Scene/SceneManager.h"



namespace ya
{

Entity::operator bool() const
{
    return _entityHandle != entt::null &&
           _scene &&
           _scene->isValid() &&
           _scene->isValidEntity(this);
}

} // namespace ya
