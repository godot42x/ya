#include "GameRuntime/Script/GameplayScriptFunctions.h"

#include "GameRuntime/App.h"

#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Component/2D/Sprite2DComponent.h"
#include "ECS/Component/2D/TilemapComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "Scene3D/TransformComponent.h"

#include <cmath>
#include <format>

namespace ya
{
namespace
{

using script::ScriptArgs;
using script::ScriptError;
using script::ScriptRef;
using script::ScriptValue;

void expectArgCount(ScriptArgs args, size_t count)
{
    if (args.size() != count) {
        throw ScriptError(std::format("expects {} argument(s), got {}", count, args.size()));
    }
}

ScriptValue find(ScriptArgs args)
{
    expectArgCount(args, 1);
    App*   app   = App::get();
    Scene* scene = app ? app->getSceneServices().getActiveScene() : nullptr;
    if (!scene) {
        throw ScriptError("no active scene");
    }
    const Entity* entity = scene->getEntityByName(script::scriptToString(args[0]));
    if (!entity) {
        return {};
    }
    return script::entityRef(entity);
}

ScriptValue viewSize(ScriptArgs args)
{
    expectArgCount(args, 0);
    App* app = App::get();
    if (!app) {
        return glm::vec2(0.0f);
    }
    const Extent2D resolution = app->getRenderServices().getRenderResolution();
    return glm::vec2(static_cast<float>(resolution.width), static_cast<float>(resolution.height));
}

/// `map:entityAt(x, y)` — the first actor in the cell (rpg-prototype R2a): an
/// entity carrying both a sprite and a script whose world position lands on
/// that cell. "Whom do I face" and "is this cell occupied" are the two
/// callers; the asking script compares identity when it must exclude itself.
/// The scene is walked linearly until a real map needs an index (D-T4), and
/// each candidate asks the map's own worldToCell, so actor and movement share
/// one cell definition.
ScriptValue entityAtCell(void* self, const ScriptRef&, ScriptArgs args)
{
    expectArgCount(args, 2);
    auto&   map   = *static_cast<TilemapComponent*>(self);
    Entity* owner = map.getOwner();
    Scene*  scene = owner ? owner->getScene() : nullptr;
    if (!scene || !map.isValid()) {
        return {};
    }
    const int32_t x = static_cast<int32_t>(script::scriptToInteger(args[0]));
    const int32_t y = static_cast<int32_t>(script::scriptToInteger(args[1]));

    auto view = scene->getRegistry().view<TransformComponent, Sprite2DComponent, LuaScriptComponent>();
    for (auto [handle, transform, sprite, scripts] : view.each()) {
        (void)sprite;
        (void)scripts;
        TransformSystem::computeWorldMatrix(&transform);
        const glm::vec2 cell = map.worldToCell(glm::vec2(transform.getTransform()[3]));
        if (static_cast<int32_t>(std::lround(cell.x)) == x && static_cast<int32_t>(std::lround(cell.y)) == y) {
            Entity* found = scene->getEntityByEnttID(handle);
            return found && scene->isValidEntity(found) ? ScriptValue{script::entityRef(found)} : ScriptValue{};
        }
    }
    return {};
}

} // namespace

void registerGameplayScriptFunctions()
{
    script::registerModuleFunction("world", "find", &find);
    script::registerModuleFunction("world", "viewSize", &viewSize);
    // A native method on the map component rather than a world function: the
    // cell questions (worldToCell / isSolid / bounds) all live on the map, so
    // "who is in this cell" is asked of the map too.
    script::addNativeMethod(type_index_v<TilemapComponent>, "entityAt", &entityAtCell);
}

} // namespace ya
