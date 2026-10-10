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
    std::function<bool(std::string path)>                                       saveWorld;
    std::function<bool(std::string path)>                                       loadWorld;
    /// Read when a script calls `world.scriptOf`.
    /// Null until the host has created the runtime; an empty function means
    /// those calls have no Lua state.
    std::function<IWorldScriptRuntime*()>                    worldRuntime;
    /// `ui.get`. Null returns nil.
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
///   world.save(path) -> call `onSave` on loaded world scripts, then write the
///       scene path and `Persist`. `path` is project-relative or absolute.
///   world.load(path) -> queue a restore for the frame-end flush. The saved
///       scene is loaded with no spawn placement, then `Persist` is replaced.
///       The next world-script init sees `world.saveRestored()` as true.
///   world.saveRestored() -> true during the init pass that follows a load.
///   world.viewAspect() -> the same presented view as viewSize, as a number
///   world.spawnSprite(name[, { texture, size, tint }]) -> entity, or nil when
///       there is no scene. A missing or blank texture leaves the sprite with
///       no image (Sprite2D does not draw it). `size` defaults to 1×1 and
///       `tint` to white. There is no probe-texture fallback.
///   world.destroyEntity(entity) -> nil. Queues the entity's node.
///   world.scriptOf(entity[, path]) -> the live self table (ScriptSelfHandle),
///       or nil. `path` filters by normalized script path. The table is for
///       this call only: a hot reload replaces it.
///   ui.get(entryId) -> the entry script's self table, else the root widget
///       handle, else nil
///
/// `entityAt` is registered here too. It does not use `ctx`: the map answers
/// from its owning scene.
YA_GAME_RUNTIME_API void registerGameplayScriptFunctions(GameplayScriptContext ctx = {});

/// Widget methods, `TilemapComponent::entityAt`, and `Entity::call`.
/// Idempotent. `registerGameplayScriptFunctions` calls this first; the widget
/// surface and Lua init call the pieces they can see so a test that never
/// builds an App still exports them before the type's first use.
YA_GAME_RUNTIME_API void registerRuntimeScriptNatives();

/// Places the entity named `actorName` on the spawn marker named `spawnName`
/// (a Node3D at scene root; only its position is taken). z stays the actor's.
/// A missing marker, an empty `actorName`, or a missing entity logs a warning
/// and leaves the scene as authored. The name comes from the caller.
YA_GAME_RUNTIME_API void placePlayerAtSpawn(Scene& scene, const std::string& spawnName, const std::string& actorName);

} // namespace ya
