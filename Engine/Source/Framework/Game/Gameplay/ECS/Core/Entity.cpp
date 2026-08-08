#include "Entity.h"
#include "Component.h"
#include "ECS/ECSRegistry.h"

#include <stdexcept>

namespace ya
{

namespace detail
{
namespace
{
EntityRenameFn g_entityRename     = nullptr;
EntityValidFn  g_entitySceneValid = nullptr;
}

void entityRenameViaScene(Entity& entity, const std::string& newName)
{
    if (g_entityRename) {
        g_entityRename(entity, newName);
    }
}

bool entityIsSceneValid(const Entity& entity)
{
    return g_entitySceneValid ? g_entitySceneValid(entity) : true;
}

void setEntitySceneBridge(EntityRenameFn rename, EntityValidFn isValid)
{
    g_entityRename     = rename;
    g_entitySceneValid = isValid;
}

} // namespace detail

void Entity::setName(const std::string& newName)
{
    name = newName;
    if (_scene) {
        detail::entityRenameViaScene(*this, newName);
    }
}

Entity::operator bool() const
{
    if (_entityHandle == entt::null || !_registry) {
        return false;
    }
    if (_scene && !detail::entityIsSceneValid(*this)) {
        return false;
    }
    return _registry->valid(_entityHandle);
}

bool Entity::hasComponentByName(const std::string& typeName) const
{
    if (_registry == nullptr) {
        return false;
    }
    return ECSRegistry::get().hasComponent(FName(typeName), *_registry, _entityHandle);
}

InstanceRef Entity::componentByName(const std::string& typeName)
{
    if (_registry == nullptr) {
        return {};
    }
    auto&      ecs       = ECSRegistry::get();
    const auto typeIndex = ecs.getTypeIndex(FName(typeName));
    if (!typeIndex) {
        throw std::runtime_error("unknown component type: " + typeName);
    }
    return InstanceRef{*typeIndex, ecs.getComponent(*typeIndex, *_registry, _entityHandle)};
}

InstanceRef Entity::addComponentByName(const std::string& typeName)
{
    if (_registry == nullptr) {
        return {};
    }
    auto&      ecs       = ECSRegistry::get();
    const auto typeIndex = ecs.getTypeIndex(FName(typeName));
    if (!typeIndex) {
        throw std::runtime_error("unknown component type: " + typeName);
    }
    void* ptr = ecs.getComponent(*typeIndex, *_registry, _entityHandle);
    if (ptr == nullptr) {
        ptr = ecs.addComponent(*typeIndex, *_registry, _entityHandle);
    }
    return InstanceRef{*typeIndex, ptr};
}

bool Entity::removeComponentByName(const std::string& typeName)
{
    if (_registry == nullptr) {
        return false;
    }
    return ECSRegistry::get().removeComponent(FName(typeName), *_registry, _entityHandle);
}

nlohmann::json Entity::components() const
{
    nlohmann::json out = nlohmann::json::object();
    if (_registry == nullptr) {
        return out;
    }

    auto& ecs = ECSRegistry::get();
    for (const auto& [fname, typeIndex] : ecs.getTypeIndexCache()) {
        if (void* ptr = ecs.getComponent(typeIndex, *_registry, _entityHandle); ptr != nullptr) {
            out[fname.toString()] = serializeInstanceRef({typeIndex, ptr});
        }
    }
    return out;
}

} // namespace ya
