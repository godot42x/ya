#include "Entity.h"
#include "Component.h"
#include "Framework/Game/Gameplay/ECS/ECSRegistry.h"
#include "Framework/Game/Render/Render3D/Scene.h"

#include <stdexcept>

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

bool Entity::hasComponentByName(const std::string& typeName) const
{
    if (_scene == nullptr) {
        return false;
    }
    return ECSRegistry::get().hasComponent(FName(typeName), *_scene, _entityHandle);
}

InstanceRef Entity::componentByName(const std::string& typeName)
{
    if (_scene == nullptr) {
        return {};
    }
    auto&      ecs       = ECSRegistry::get();
    const auto typeIndex = ecs.getTypeIndex(FName(typeName));
    if (!typeIndex) {
        throw std::runtime_error("unknown component type: " + typeName);
    }
    return InstanceRef{*typeIndex, ecs.getComponent(*typeIndex, *_scene, _entityHandle)};
}

InstanceRef Entity::addComponentByName(const std::string& typeName)
{
    if (_scene == nullptr) {
        return {};
    }
    auto&      ecs       = ECSRegistry::get();
    const auto typeIndex = ecs.getTypeIndex(FName(typeName));
    if (!typeIndex) {
        throw std::runtime_error("unknown component type: " + typeName);
    }
    void* ptr = ecs.getComponent(*typeIndex, *_scene, _entityHandle);
    if (ptr == nullptr) {
        ptr = ecs.addComponent(*typeIndex, *_scene, _entityHandle);
    }
    return InstanceRef{*typeIndex, ptr};
}

bool Entity::removeComponentByName(const std::string& typeName)
{
    if (_scene == nullptr) {
        return false;
    }
    return ECSRegistry::get().removeComponent(FName(typeName), *_scene, _entityHandle);
}

nlohmann::json Entity::components() const
{
    nlohmann::json out = nlohmann::json::object();
    if (_scene == nullptr) {
        return out;
    }

    auto& ecs = ECSRegistry::get();
    for (const auto& [fname, typeIndex] : ecs.getTypeIndexCache()) {
        if (void* ptr = ecs.getComponent(typeIndex, *_scene, _entityHandle); ptr != nullptr) {
            out[fname.toString()] = serializeInstanceRef({typeIndex, ptr});
        }
    }
    return out;
}

} // namespace ya
