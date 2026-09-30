#pragma once

#include "ECS/SceneBus.h"
#include "Core/TypeIndex.h"

#include <concepts>
#include <entt/entt.hpp>
#include <utility>

namespace ya
{

/// The entity that holds a component. Declared here because the creation
/// funnel below needs to name it; the definition lives in ECS/Entity.h.
struct Entity;

} // namespace ya

namespace ya::detail_component_mutation
{
/// The one place a component instance is created.
///
/// A component has to know the entity that owns it. The registry cannot answer that
/// question on its own -- `Entity` wrappers belong to the Scene -- so ownership
/// is an argument here rather than something the caller patches afterwards.
/// Every creation funnel (typed, name-based, type-erased) passes through this
/// function, so no path can leave the back-pointer unset, and "created without
/// an owner" is not expressible.
template <typename ComponentType, typename... Args>
ComponentType* addComponent(entt::registry& registry, entt::entity entity, Entity* owner, Args&&... args)
{
    ComponentType* component = &registry.emplace<ComponentType>(entity, std::forward<Args>(args)...);
    if (component) {
        component->setOwner(owner);
        SceneBus::get().onComponentAdded.broadcast(registry, entity, type_index_v<ComponentType>);
    }
    return component;
}

template <typename ComponentType>
bool removeComponent(entt::registry& registry, entt::entity entity)
{
    if (!registry.all_of<ComponentType>(entity)) {
        return false;
    }

    if constexpr (requires(ComponentType& component) { component.prepareForRemove(); }) {
        if (auto* component = registry.try_get<ComponentType>(entity)) {
            component->prepareForRemove();
        }
    }

    registry.remove<ComponentType>(entity);
    SceneBus::get().onComponentRemoved.broadcast(registry, entity, type_index_v<ComponentType>);
    return true;
}

} // namespace ya::detail_component_mutation
