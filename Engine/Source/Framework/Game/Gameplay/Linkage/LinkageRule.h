#pragma once

#include "Core/Api.h"
#include "Core/TypeIndex.h"

#include <entt/entt.hpp>

namespace ya
{

struct Scene;
class LinkageFramework;

/**
 * @brief Linkage rule: reacts to component construct/update/destroy for the
 * component types it watches and runs its work on the framework's deferred
 * frame-task scheduler (scene-validity checked).
 *
 * Rules are business logic (light billboards, material topology, ...); the
 * framework only dispatches events and schedules deferred work. Rules never
 * reach Host or the app singleton.
 */
struct ILinkageRule
{
    virtual ~ILinkageRule() = default;

    /// Wire entt component signals for a scene registry and run the initial
    /// sweep. Called once per scene init.
    virtual void onSceneInit(Scene* scene) = 0;

    /// Component removal dispatch (SceneBus). `type` is the removed component
    /// type index; `entity` may still be valid.
    virtual void onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type) = 0;

    /// The scene is being destroyed (fired before its registry dies). Rules
    /// must disconnect their entt signal connections for this scene's
    /// registry so teardown events never reach a rule that may already be
    /// gone (e.g. framework shut down before the scene).
    virtual void onSceneUnload(Scene* scene) {}
};

} // namespace ya
