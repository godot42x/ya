/**
 * @file ManagedChildComponent.h
 * @brief Marker component for dynamically managed child entities
 *
 * Design:
 * - Tag component that marks an Entity as "managed by a parent component"
 * - Entities with this component are SKIPPED during serialization
 * - They will be recreated at runtime by their parent system (e.g., ModelInstantiationSystem)
 *
 * Use cases:
 * - ModelComponent creates child entities (one per mesh) via ModelInstantiationSystem
 * - Any future system that dynamically creates child entities
 *
 * This is the ECS-native approach (similar to Unity DOTS LinkedEntityGroup,
 * Bevy's DespawnOnExitMarker, or Flecs' parent-child patterns).
 */
#pragma once

#include "ECS/Component.h"

namespace ya
{

struct ManagedChildComponent : public IComponent
{
    YA_REFLECT_BEGIN(ManagedChildComponent)
    YA_REFLECT_END()

    // No serialized fields — the component's presence IS the signal. The host
    // handle is runtime-only as well: the serializer skips the entity by
    // component presence, and the host rebuilds its companions on load, so
    // there is nothing to persist.

    /// Entity whose system generated this one. Null means "generated, host
    /// unknown" (a companion whose host died); consumers resolve provenance
    /// through CompanionRegistry, never by inspecting component fields.
    entt::entity host = entt::null;
};

} // namespace ya
