#include "ECSRegistry.h"

#include "Scene/Scene.h"

namespace ya
{

ECSRegistry& ECSRegistry::get()
{
    static ECSRegistry instance;
    return instance;
}

bool ECSRegistry::hasComponent(ya::type_index_t typeIndex, const Scene& scene, entt::entity entity)
{
    return hasComponent(typeIndex, scene.getRegistry(), entity);
}

bool ECSRegistry::hasComponent(FName name, const Scene& scene, entt::entity entity)
{
    return hasComponent(name, scene.getRegistry(), entity);
}

void* ECSRegistry::getComponent(ya::type_index_t typeIndex, const Scene& scene, entt::entity entity)
{
    return getComponent(typeIndex, scene.getRegistry(), entity);
}

void* ECSRegistry::getComponent(FName name, const Scene& scene, entt::entity entity)
{
    return getComponent(name, scene.getRegistry(), entity);
}

void* ECSRegistry::addComponent(ya::type_index_t typeIndex, Scene& scene, entt::entity entity)
{
    if (auto opsIt = _componentOps.find(typeIndex); opsIt != _componentOps.end()) {
        return opsIt->second->create(scene.getRegistry(), entity);
    }
    return nullptr;
}

void* ECSRegistry::addComponent(FName name, Scene& scene, entt::entity entity)
{
    if (auto typeIndex = getTypeIndex(name)) {
        return addComponent(typeIndex.value(), scene, entity);
    }
    return nullptr;
}

bool ECSRegistry::removeComponent(ya::type_index_t typeIndex, Scene& scene, entt::entity entity)
{
    if (auto opsIt = _componentOps.find(typeIndex); opsIt != _componentOps.end()) {
        return opsIt->second->remove(scene.getRegistry(), entity);
    }
    return false;
}

bool ECSRegistry::removeComponent(FName name, Scene& scene, entt::entity entity)
{
    if (auto typeIndex = getTypeIndex(name)) {
        return removeComponent(typeIndex.value(), scene, entity);
    }
    return false;
}

} // namespace ya
