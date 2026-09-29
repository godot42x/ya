#pragma once

#include "Core/Scripting/ScriptValue.h"

namespace ya
{
struct Scene;
struct Entity;
} // namespace ya

namespace ya::script
{

/// Registers the scene, entity and component reference kinds and the entity's
/// native script methods: `get/has/add/remove(typeName)` plus
/// `get<Short>()` / `has<Short>()` for every registered component, where Short
/// is the component name without its `Component` suffix. Idempotent; every
/// script backend calls it before exporting objects.
YA_SCENE_CORE_API void ensureSceneScriptBindings();

[[nodiscard]] YA_SCENE_CORE_API ScriptRef sceneRef(const Scene* scene);
[[nodiscard]] YA_SCENE_CORE_API ScriptRef entityRef(const Entity* entity);
[[nodiscard]] YA_SCENE_CORE_API ScriptRef componentRef(const Entity* entity, type_index_t componentType);

/// Null when the reference is not of that kind or its object is gone.
[[nodiscard]] YA_SCENE_CORE_API Scene*  sceneOf(const ScriptRef& ref);
[[nodiscard]] YA_SCENE_CORE_API Entity* entityOf(const ScriptRef& ref);

} // namespace ya::script
