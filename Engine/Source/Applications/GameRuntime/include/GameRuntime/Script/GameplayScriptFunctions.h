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
    std::function<void(std::string path, std::string spawn)> requestSceneTransfer;
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
///   world.loadScene(path, spawnName) -> switch scenes at the frame-end
///       structural flush, then place the entity named "Player" on the spawn
///       marker `spawnName` (rpg R3)
///
/// `entityAt` is registered here too. It does not use `ctx`: the map answers
/// from its owning scene.
YA_GAME_RUNTIME_API void registerGameplayScriptFunctions(GameplayScriptContext ctx = {});

/// Places the scene's "Player" entity on the spawn marker named `spawnName`
/// (a Node3D at scene root; only its position is taken). A missing marker or
/// player logs a warning and leaves the scene as authored — the gameplay
/// actor's name is the contract, nothing else is hardwired.
YA_GAME_RUNTIME_API void placePlayerAtSpawn(Scene& scene, const std::string& spawnName);

} // namespace ya
