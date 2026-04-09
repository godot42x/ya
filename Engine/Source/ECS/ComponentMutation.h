#pragma once

#include "Bus/SceneBus.h"
#include "Core/TypeIndex.h"

#include <concepts>
#include <entt/entt.hpp>
#include <utility>

namespace ya::detail_component_mutation
{

template <typename ComponentType, typename... Args>
ComponentType* addComponent(entt::registry& registry, entt::entity entity, Args&&... args)
{
    return &registry.emplace<ComponentType>(entity, std::forward<Args>(args)...);
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
