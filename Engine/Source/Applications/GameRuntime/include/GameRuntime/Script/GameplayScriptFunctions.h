#pragma once

#include "Core/Api.h"

#include <glm/vec2.hpp>

#include <functional>
#include <string>

namespace ya
{

struct Scene;
struct CameraComponent;
class InputRouter;

/// Injected by the host when gameplay module functions are registered.
/// The functions close over this; they do not look up `App::get()`.
/// An empty context is "no running app": `find` reports no scene,
/// `loadScene` throws, and view queries see a zero-size frame.
struct GameplayScriptContext
{
    std::function<Scene*()>                                  activeScene;
    std::function<glm::vec2()>                               presentedPixels;
    /// `spawn` is the destination marker; `actor` is the entity to move onto it.
    /// An empty actor moves nobody — the caller supplies the name.
    std::function<void(std::string path, std::string spawn, std::string actor)> requestSceneTransfer;
    /// `Input.setKeyHandler` captures this at bind time. Null ignores the call.
    InputRouter*                                             inputRouter = nullptr;
};

/// One read of the presented frame: pixel size from `presentedPixels`, primary
/// camera from `activeScene`. `world.viewSize` and `world.viewAspect` both
/// call this, then `scriptViewSize` / `scriptViewAspect`.
struct GameplayPresentedView
{
    glm::vec2              pixelExtent = glm::vec2(0.0f);
    const CameraComponent* camera      = nullptr;
};

[[nodiscard]] YA_GAME_RUNTIME_API GameplayPresentedView gameplayPresentedView(const GameplayScriptContext& ctx);

/// Gameplay module functions every script backend projects (`world.find` in
/// Lua, `ya.world.find` in JS). Registered through the script export
/// (Core/Scripting/ScriptBindings.h) before the backends build their states.
///
///   world.find(name)  -> the active scene's entity named `name`, or nil
///   world.viewSize()  -> pixel-perfect orthographic primary camera:
///         vec2(halfWidth, halfHeight) in world units (resolveCameraViewFraming).
///         Otherwise the presented view in pixels.
///   world.loadScene(path[, spawnName[, actorName]]) -> switch scenes at the
///       frame-end structural flush. When `spawnName` is set, place the entity
///       named `actorName` on that marker (rpg R3). An omitted or empty
///       `actorName` moves nobody.
///
/// `entityAt` is registered here too. It does not use `ctx`: the map answers
/// from its owning scene.
YA_GAME_RUNTIME_API void registerGameplayScriptFunctions(GameplayScriptContext ctx = {});

/// Places the entity named `actorName` on the spawn marker named `spawnName`
/// (a Node3D at scene root; only its position is taken). z stays the actor's.
/// A missing marker, an empty `actorName`, or a missing entity logs a warning
/// and leaves the scene as authored. The name comes from the caller.
YA_GAME_RUNTIME_API void placePlayerAtSpawn(Scene& scene, const std::string& spawnName, const std::string& actorName);

} // namespace ya
