
#pragma once


#include "Core/Api.h"
#include "Core/Delegate.h"
#include "Core/Trait.h"
#include "Core/TypeIndex.h"

#include <entt/fwd.hpp>


namespace ya
{


/// Point-to-point component-mutation broadcasts, each with the registry the
/// mutation landed in. `onComponentAdded` is published by the one creation
/// funnel (detail_component_mutation::addComponent), `onComponentRemoved` by
/// the removal funnel, and `onComponentEdited` by the scene edit funnel
/// (Scene::notifyComponentEdited -> registry.patch). Subscribers filter by
/// typeIndex and look scenes up by the registry pointer they are given.
struct SceneBus : public disable_copy
{
    static YA_ECS_CORE_API SceneBus& get();

    MulticastDelegate<void(entt::registry&, const entt::entity, ya::type_index_t)> onComponentAdded;
    MulticastDelegate<void(entt::registry&, const entt::entity, ya::type_index_t)> onComponentEdited;
    MulticastDelegate<void(entt::registry&, const entt::entity, ya::type_index_t)> onComponentRemoved;
};

} // namespace ya
