#pragma once

#include "ECS/ComponentMutation.h"

namespace ya
{

template <typename T, typename... Args>
T* Entity::addComponent(Args&&... args)
{
    static_assert(!std::is_base_of_v<T, IComponent>, "Component type must not derive from IComponent directly");
    YA_CORE_ASSERT(_registry != nullptr, "Entity registry is null!");
    YA_CORE_ASSERT(!hasComponent<T>(), "Entity already has component!");
    T* comp = detail_component_mutation::addComponent<T>(*_registry, _entityHandle, std::forward<Args>(args)...);
    YA_CORE_ASSERT(comp != nullptr, "Failed to add component!");
    static_cast<IComponent*>(comp)->setOwner(this);
    return comp;
}

template <typename T>
void Entity::removeComponent()
{
    YA_CORE_ASSERT(_registry != nullptr, "Entity registry is null!");
    assert(hasComponent<T>() && "Entity does not have component!");
    detail_component_mutation::removeComponent<T>(*_registry, _entityHandle);
}

template <typename T>
T* Entity::getComponent()
{
    YA_CORE_ASSERT(_registry != nullptr, "Entity registry is null!");
    assert(hasComponent<T>() && "Entity does not have component!");
    return &_registry->get<T>(_entityHandle);
}

template <typename T>
const T* Entity::getComponent() const
{
    YA_CORE_ASSERT(_registry != nullptr, "Entity registry is null!");
    assert(hasComponent<T>() && "Entity does not have component!");
    return &_registry->get<T>(_entityHandle);
}

template <typename T>
[[nodiscard]] bool Entity::hasComponent() const
{
    YA_CORE_ASSERT(_entityHandle != entt::null, "Entity handle is null!");
    YA_CORE_ASSERT(_registry != nullptr, "Entity registry is null!");
    return _registry->all_of<T>(_entityHandle);
}

template <typename... Components>
[[nodiscard]] bool Entity::hasComponents() const
{
    YA_CORE_ASSERT(_registry != nullptr, "Entity registry is null!");
    return _registry->all_of<Components...>(_entityHandle);
}

template <typename... Components>
auto Entity::getComponents()
{
    YA_CORE_ASSERT(_registry != nullptr, "Entity registry is null!");
    return _registry->get<Components...>(_entityHandle);
}

} // namespace ya
