#pragma once

// ============================================================================
// Entity <-> Scene contract.
//
// Entity (ya-ecs-core) keeps a forward-declared Scene* descriptor so it can
// stay a lightweight handle, but rename/validity need the Scene module's node
// tree / lifecycle tracking. ecs-core provides no-op defaults (no Scene module
// linked) and lets the scene module register the real implementation through
// setEntitySceneBridge(); ya-scene-core registers it from
// Scene/Core/EntitySceneBridge.cpp. ecs-core never depends on Scene types.
// ============================================================================

#include <string>

namespace ya
{
struct Entity;
}

namespace ya::detail
{

using EntityRenameFn  = void (*)(Entity& entity, const std::string& newName);
using EntityValidFn   = bool (*)(const Entity& entity);

/// Rename the entity's associated scene node (if any). The `name` field itself
/// is updated by Entity::setName before this is invoked. Default: no-op.
void entityRenameViaScene(Entity& entity, const std::string& newName);

/// Scene-side validity: the owning scene is alive and still tracks the entity.
/// Default: true (no scene module linked / not registered yet).
bool entityIsSceneValid(const Entity& entity);

/// Installs the scene-backed implementations. Called by ya-scene-core at
/// library load time; never called by engine code.
void setEntitySceneBridge(EntityRenameFn rename, EntityValidFn isValid);

} // namespace ya::detail
