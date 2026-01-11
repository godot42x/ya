#include "Entity.h"
#include "Component.h"

#include "Core/App/App.h"
#include "Scene/SceneManager.h"



namespace ya
{

Entity::operator bool() const
{
    return _entityHandle != entt::null &&
           App::get()->getSceneManager()->isSceneValid(_scene) &&
           _scene->isValidEntity(this);
}

} // namespace ya
