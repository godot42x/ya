#pragma once

#include "Core/Api.h"

#include <glm/vec2.hpp>

#include <functional>
#include <string>

namespace ya
{

struct Scene;
struct CameraComponent;
struct GameUIHost;
struct IWorldScriptRuntime;
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
    /// `Input.setKeyHandler` captures this at registration. Null ignores the call.
    InputRouter*                                             inputRouter = nullptr;
    /// Read when a script calls `world.scriptOf` or the key handler runs.
    /// Null until the host has created the runtime; an empty function means
    /// those calls have no Lua state.
    std::function<IWorldScriptRuntime*()>                    worldRuntime;
    /// `ui.setText` / `ui.setVisible`. Null returns false.
    GameUIHost*                                              gameUI = nullptr;
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
///   world.viewAspect() -> the same presented view as viewSize, as a number
///   world.spawnSprite(name[, texturePath]) -> entity, or nil when there is
///       no scene. A missing or blank path leaves the sprite with no texture
///       (Sprite2D does not draw it). Callers that want a picture pass a path.
///   world.destroyEntity(entity) -> nil. Queues the entity's node.
///   world.scriptOf(entity[, path]) -> the live self table (ScriptSelfHandle),
///       or nil. `path` filters by normalized script path. The table is for
///       this call only: a hot reload replaces it.
///   Input.setKeyHandler(fn | nil) -> one global key callback,
///       (key, pressed, repeat) -> bool. A non-function clears it.
///   ui.get(entryId) -> the entry script's self table, else the root widget
///       handle, else nil
///   ui.setText(entryId, widgetName, text) -> bool
///   ui.setVisible(entryId, widgetName, visible) -> bool
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
