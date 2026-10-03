#include "GameRuntime/Script/GameplayScriptFunctions.h"

#include "GameRuntime/App.h"
#include "GameRuntime/AppSceneServices.h"

#include "Core/Log.h"
#include "Core/Scripting/ScriptBindings.h"
#include "Scene2D/Sprite2DComponent.h"
#include "Scene2D/TilemapComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
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

struct PresentedView
{
    glm::vec2              pixelExtent = glm::vec2(0.0f);
    const CameraComponent* camera      = nullptr;
};

PresentedView currentPresentedView()
{
    PresentedView presented;
    App*          app = App::get();
    if (!app) {
        return presented;
    }
    const Extent2D resolution = app->getRenderServices().getRenderResolution();
    presented.pixelExtent     = glm::vec2(static_cast<float>(resolution.width), static_cast<float>(resolution.height));
    Scene*  scene             = app->getSceneServices().getActiveScene();
    Entity* cameraEntity      = scene ? findPrimaryCamera(*scene) : nullptr;
    presented.camera          = cameraEntity && cameraEntity->hasComponent<CameraComponent>()
                                    ? cameraEntity->getComponent<CameraComponent>()
                                    : nullptr;
    return presented;
}

ScriptValue viewSize(ScriptArgs args)
{
    expectArgCount(args, 0);
    const PresentedView presented = currentPresentedView();
    return scriptViewSize(presented.camera, presented.pixelExtent);
}

/// `map:entityAt(x, y[, except])` — the first actor in the cell (rpg-prototype
/// R2a): an entity carrying both a sprite and a script whose world position
/// lands on that cell. "Whom do I face" and "is this cell occupied" are the
/// two callers; the optional `except` entity ref lets a walker query its own
/// cell for the event it just stepped onto. The asking script compares
/// identity when it must exclude itself otherwise. The scene is walked
/// linearly until a real map needs an index (D-T4), and each candidate asks
/// the map's own worldToCell, so actor and movement share one cell
/// definition.
ScriptValue entityAtCell(void* self, const ScriptRef&, ScriptArgs args)
{
    if (args.size() != 2 && args.size() != 3) {
        throw ScriptError("expects entityAt(x, y) or entityAt(x, y, except)");
    }
    auto&   map   = *static_cast<TilemapComponent*>(self);
    Entity* owner = map.getOwner();
    Scene*  scene = owner ? owner->getScene() : nullptr;
    if (!scene || !map.isValid()) {
        return {};
    }
    const int32_t x = static_cast<int32_t>(script::scriptToInteger(args[0]));
    const int32_t y = static_cast<int32_t>(script::scriptToInteger(args[1]));
    const ScriptRef exceptRef = args.size() == 3 && std::holds_alternative<ScriptRef>(args[2])
                                    ? std::get<ScriptRef>(args[2])
                                    : ScriptRef{};
    Entity* const except = script::entityOf(exceptRef);

    auto view = scene->getRegistry().view<TransformComponent, Sprite2DComponent, LuaScriptComponent>();
    for (auto [handle, transform, sprite, scripts] : view.each()) {
        (void)sprite;
        (void)scripts;
        Entity* candidate = scene->getEntityByEnttID(handle);
        if (candidate == nullptr || candidate == except || !scene->isValidEntity(candidate)) {
            continue;
        }
        TransformSystem::computeWorldMatrix(&transform);
        const glm::vec2 cell = map.worldToCell(glm::vec2(transform.getTransform()[3]));
        if (static_cast<int32_t>(std::lround(cell.x)) == x && static_cast<int32_t>(std::lround(cell.y)) == y) {
            return ScriptValue{script::entityRef(candidate)};
        }
    }
    return {};
}

} // namespace

float gameplayViewAspect()
{
    const PresentedView presented = currentPresentedView();
    return scriptViewAspect(presented.camera, presented.pixelExtent);
}

void placePlayerAtSpawn(Scene& scene, const std::string& spawnName)
{
    Entity* marker = scene.getEntityByName(spawnName);
    if (!marker) {
        YA_CORE_WARN("placePlayerAtSpawn: no spawn marker named '{}'", spawnName);
        return;
    }
    Entity* player = scene.getEntityByName("Player");
    if (!player) {
        YA_CORE_WARN("placePlayerAtSpawn: no entity named 'Player' in this scene");
        return;
    }
    auto* markerTransform = marker->hasComponent<TransformComponent>() ? marker->getComponent<TransformComponent>() : nullptr;
    auto* playerTransform = player->hasComponent<TransformComponent>() ? player->getComponent<TransformComponent>() : nullptr;
    if (!markerTransform || !playerTransform) {
        return;
    }
    // Spawn markers live at scene root, so the local position is the world
    // position; z stays the player's own (it sets its depth itself).
    const glm::vec3 spawn = markerTransform->getPosition();
    const glm::vec3 current = playerTransform->getPosition();
    playerTransform->setPosition({spawn.x, spawn.y, current.z});
}

void registerGameplayScriptFunctions()
{
    script::registerModuleFunction("world", "find", &find);
    script::registerModuleFunction("world", "viewSize", &viewSize);
    // A native method on the map component rather than a world function: the
    // cell questions (worldToCell / isSolid / bounds) all live on the map, so
    // "who is in this cell" is asked of the map too.
    script::addNativeMethod(type_index_v<TilemapComponent>, "entityAt", &entityAtCell);
    script::registerModuleFunction("world", "loadScene", [](script::ScriptArgs args) -> ScriptValue {
        if (args.empty() || args.size() > 2) {
            throw ScriptError("expects loadScene(path) or loadScene(path, spawnName)");
        }
        App* app = App::get();
        if (!app) {
            throw ScriptError("no running app");
        }
        app->getSceneServices().requestSceneTransfer(script::scriptToString(args[0]),
                                                     args.size() > 1 ? script::scriptToString(args[1]) : std::string{});
        return {};
    });
}

} // namespace ya
