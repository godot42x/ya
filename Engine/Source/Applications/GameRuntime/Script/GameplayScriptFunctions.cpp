#include "GameRuntime/Script/GameplayScriptFunctions.h"

#include "Core/Log.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Scripting/ScriptBindings.h"
#include "Core/System/VirtualFileSystem.h"
#include "Scene2D/Sprite2DComponent.h"
#include "Scene2D/Text2DComponent.h"
#include "Scene2D/ParticleEmitterComponent.h"
#include "Scene2D/TilemapComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "ECS/Systems/WorldScriptRuntime.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "LuaWidgetHandle.h"
#include "LuaWidgetScripts.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "Audio/AudioSystem.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "Script/Lua/LuaBackend.h"
#include "Scene3D/Node3D.h"
#include "Scene3D/TransformComponent.h"
#include "WidgetScriptRefs.h"

#include <glm/glm.hpp>

#include <cmath>
#include <format>

namespace ya
{

GameplayPresentedView gameplayPresentedView(const GameplayScriptContext& ctx)
{
    GameplayPresentedView presented;
    if (ctx.presentedPixels) {
        presented.pixelExtent = ctx.presentedPixels();
    }
    Scene*        scene        = ctx.activeScene ? ctx.activeScene() : nullptr;
    Entity*       cameraEntity = scene ? findPrimaryCamera(*scene) : nullptr;
    presented.camera           = cameraEntity && cameraEntity->hasComponent<CameraComponent>()
                                     ? cameraEntity->getComponent<CameraComponent>()
                                     : nullptr;
    return presented;
}

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

ScriptValue find(const GameplayScriptContext& ctx, ScriptArgs args)
{
    expectArgCount(args, 1);
    Scene* scene = ctx.activeScene ? ctx.activeScene() : nullptr;
    if (!scene) {
        throw ScriptError("no active scene");
    }
    const Entity* entity = scene->getEntityByName(script::scriptToString(args[0]));
    if (!entity) {
        return {};
    }
    return script::scriptMake(script::entityRef(entity));
}

ScriptValue viewSize(const GameplayScriptContext& ctx, ScriptArgs args)
{
    expectArgCount(args, 0);
    const GameplayPresentedView presented = gameplayPresentedView(ctx);
    return script::scriptMake(scriptViewSize(presented.camera, presented.pixelExtent));
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
ScriptValue entityAtCell(const TilemapComponent& map, ScriptArgs args)
{
    if (args.size() != 2 && args.size() != 3) {
        throw ScriptError("expects entityAt(x, y) or entityAt(x, y, except)");
    }
    Entity* owner = map.getOwner();
    Scene*  scene = owner ? owner->getScene() : nullptr;
    if (!scene || !map.isValid()) {
        return {};
    }
    const int32_t x = static_cast<int32_t>(script::scriptToInteger(args[0]));
    const int32_t y = static_cast<int32_t>(script::scriptToInteger(args[1]));
    const ScriptRef exceptRef = args.size() == 3 && (script::scriptIf<ScriptRef>(args[2]) != nullptr)
                                    ? (*script::scriptIf<ScriptRef>(args[2]))
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
            return script::scriptMake(script::entityRef(candidate));
        }
    }
    return {};
}

/// Options table for `world.spawnSprite`. A missing or empty `texture` leaves
/// the sprite with no image. `size` defaults to 1×1, `tint` to white.
struct FSpriteSpawnOptions
{
    std::string texture;
    glm::vec2   size{1.0f, 1.0f};
    glm::vec4   tint{1.0f, 1.0f, 1.0f, 1.0f};

    YA_REFLECT_BEGIN_NAMED(FSpriteSpawnOptions, "FSpriteSpawnOptions")
    YA_REFLECT_FIELD(texture)
    YA_REFLECT_FIELD(size)
    YA_REFLECT_FIELD(tint)
    YA_REFLECT_END()
    YA_REFLECT_VALUE_STRUCT()
};

type_index_t spriteSpawnOptionsType()
{
    FSpriteSpawnOptions::__ensure_reflection_registered();
    return type_index_v<FSpriteSpawnOptions>;
}

/// A caller-supplied path is stored through `TextureSlot::fromPath` (the same
/// asset-path form as a scene sprite). An empty path leaves the slot unset:
/// the engine does not invent a texture, and Sprite2D does not draw the sprite.
Entity* spawnSprite(const GameplayScriptContext& ctx, const std::string& name, const FSpriteSpawnOptions& options)
{
    Scene* scene = ctx.activeScene ? ctx.activeScene() : nullptr;
    if (!scene) {
        YA_CORE_ERROR("world.spawnSprite: no active scene");
        return nullptr;
    }
    Node3D* node = scene->createNode3D(name.empty() ? "Sprite" : name);
    if (!node || !node->getEntity()) {
        return nullptr;
    }
    Sprite2DComponent* sprite = node->getEntity()->addComponent<Sprite2DComponent>();
    if (!options.texture.empty()) {
        sprite->image.fromPath(options.texture);
    }
    sprite->size = options.size;
    sprite->tint = options.tint;
    return node->getEntity();
}

void destroySpriteEntity(const GameplayScriptContext& ctx, Entity* entity)
{
    Scene* scene = ctx.activeScene ? ctx.activeScene() : nullptr;
    if (!scene || !entity) {
        return;
    }
    if (Node* node = scene->getNodeByEntity(entity)) {
        scene->queueDestroyNode(node);
    }
}

std::string optionalString(const ScriptValue& value)
{
    if ((value).isNull()) {
        return {};
    }
    return script::scriptToString(value);
}

ScriptValue spawnSpriteFn(const GameplayScriptContext& ctx, ScriptArgs args)
{
    if (args.empty() || args.size() > 2) {
        throw ScriptError("expects spawnSprite(name) or spawnSprite(name, { texture, size, tint })");
    }
    const std::string name = script::scriptToString(args[0]);
    FSpriteSpawnOptions options;
    if (args.size() > 1) {
        const FSpriteSpawnOptions* table = script::scriptIf<FSpriteSpawnOptions>(args[1]);
        if (table == nullptr) {
            throw ScriptError("expects spawnSprite(name, { texture, size, tint })");
        }
        options = *table;
    }
    Entity* entity = spawnSprite(ctx, name, options);
    if (!entity) {
        return {};
    }
    return script::scriptMake(script::entityRef(entity));
}

ScriptValue destroyEntityFn(const GameplayScriptContext& ctx, ScriptArgs args)
{
    expectArgCount(args, 1);
    if (!(script::scriptIf<ScriptRef>(args[0]) != nullptr)) {
        throw ScriptError("expected an entity");
    }
    destroySpriteEntity(ctx, script::entityOf((*script::scriptIf<ScriptRef>(args[0]))));
    return {};
}

/// Options table for `world.spawnText`. `size` is the world height of one text
/// line; `color` is the ink (alpha fades the line); `fontPixelSize` is the
/// device pixel size the face is rasterized at (fixed while the text lives).
struct FTextSpawnOptions
{
    std::string font;
    float       size          = 0.5f;
    glm::vec4   color{1.0f, 1.0f, 1.0f, 1.0f};
    int64_t     fontPixelSize = 32;

    YA_REFLECT_BEGIN_NAMED(FTextSpawnOptions, "FTextSpawnOptions")
    YA_REFLECT_FIELD(font)
    YA_REFLECT_FIELD(size)
    YA_REFLECT_FIELD(color)
    YA_REFLECT_FIELD(fontPixelSize)
    YA_REFLECT_END()
    YA_REFLECT_VALUE_STRUCT()
};

type_index_t textSpawnOptionsType()
{
    FTextSpawnOptions::__ensure_reflection_registered();
    return type_index_v<FTextSpawnOptions>;
}

/// A world-space label at the given world position (WT1). The entity carries
/// Text2DComponent beside a Transform, expanded into glyph sprite candidates
/// by the frame extractor.
Entity* spawnText(const GameplayScriptContext& ctx, const std::string& name, const glm::vec3& position,
                  const std::string& text, const FTextSpawnOptions& options)
{
    Scene* scene = ctx.activeScene ? ctx.activeScene() : nullptr;
    if (!scene) {
        YA_CORE_ERROR("world.spawnText: no active scene");
        return nullptr;
    }
    Node3D* node = scene->createNode3D(name.empty() ? "Text" : name);
    if (!node || !node->getEntity()) {
        return nullptr;
    }
    if (TransformComponent* transform = node->getEntity()->tryGetComponent<TransformComponent>()) {
        transform->setPosition(position);
    }
    Text2DComponent* label = node->getEntity()->addComponent<Text2DComponent>();
    label->text          = text;
    label->font          = options.font;
    label->size          = options.size;
    label->color         = options.color;
    label->fontPixelSize = options.fontPixelSize > 0 ? static_cast<uint32_t>(options.fontPixelSize) : 32;
    return node->getEntity();
}

ScriptValue spawnTextFn(const GameplayScriptContext& ctx, ScriptArgs args)
{
    if (args.size() < 3 || args.size() > 4) {
        throw ScriptError("expects spawnText(name, position, text[, { font, size, color, fontPixelSize }])");
    }
    const std::string name = script::scriptToString(args[0]);
    const auto*       pos  = script::scriptIf<glm::vec3>(args[1]);
    if (pos == nullptr) {
        throw ScriptError("expects spawnText(name, position, text[, options]); position is a Vec3");
    }
    const std::string text = script::scriptToString(args[2]);
    FTextSpawnOptions options;
    if (args.size() > 3) {
        const FTextSpawnOptions* table = script::scriptIf<FTextSpawnOptions>(args[3]);
        if (table == nullptr) {
            throw ScriptError("expects spawnText(name, position, text[, { font, size, color, fontPixelSize }])");
        }
        options = *table;
    }
    Entity* entity = spawnText(ctx, name, *pos, text, options);
    if (!entity) {
        return {};
    }
    return script::scriptMake(script::entityRef(entity));
}

/// Options for `render.setLook`. Every field is optional; the ones a caller
/// sets are applied and the rest keep their current value. Ranges are the
/// shader's: exposure is a multiplier, threshold/strength are unbounded
/// positives. `toneMap` / `gamma` toggle the encode; `bloom` toggles bloom.
struct FLookOptions
{
    bool     bToneMap       = true;
    bool     bGamma         = true;
    float    exposure       = 0.6f;
    bool     bBloom         = false;
    float    bloomThreshold = 1.0f;
    float    bloomStrength  = 0.8f;
    int64_t  grayscale      = 0;   // 0 none, 1 average, 2 weighted
    int64_t  kernel         = 0;   // 0 none, 1 sharpen, 2 blur, 3 edge
    bool     bGrain         = false;
    bool     bInversion     = false;

    YA_REFLECT_BEGIN_NAMED(FLookOptions, "FLookOptions")
    YA_REFLECT_FIELD(bToneMap)
    YA_REFLECT_FIELD(bGamma)
    YA_REFLECT_FIELD(exposure)
    YA_REFLECT_FIELD(bBloom)
    YA_REFLECT_FIELD(bloomThreshold)
    YA_REFLECT_FIELD(bloomStrength)
    YA_REFLECT_FIELD(grayscale)
    YA_REFLECT_FIELD(kernel)
    YA_REFLECT_FIELD(bGrain)
    YA_REFLECT_FIELD(bInversion)
    YA_REFLECT_END()
    YA_REFLECT_VALUE_STRUCT()
};

type_index_t lookOptionsType()
{
    FLookOptions::__ensure_reflection_registered();
    return type_index_v<FLookOptions>;
}

/// Options for `audio.play`. `bus` is "master"/"music"/"sfx"/"ui"; `volume` and
/// `pitch` default to 1; `loop` defaults to false.
struct FAudioPlayOptions
{
    std::string bus    = "sfx";
    float       volume = 1.0f;
    float       pitch  = 1.0f;
    bool        bLoop  = false;

    YA_REFLECT_BEGIN_NAMED(FAudioPlayOptions, "FAudioPlayOptions")
    YA_REFLECT_FIELD(bus)
    YA_REFLECT_FIELD(volume)
    YA_REFLECT_FIELD(pitch)
    YA_REFLECT_FIELD(bLoop)
    YA_REFLECT_END()
    YA_REFLECT_VALUE_STRUCT()
};

type_index_t audioPlayOptionsType()
{
    FAudioPlayOptions::__ensure_reflection_registered();
    return type_index_v<FAudioPlayOptions>;
}

EAudioBus audioBusNamed(const std::string& name)
{
    if (name == "master") {
        return EAudioBus::Master;
    }
    if (name == "music") {
        return EAudioBus::Music;
    }
    if (name == "ui") {
        return EAudioBus::Ui;
    }
    return EAudioBus::Sfx;
}

/// A script names a clip by its asset path (the same "Content/..." form as a
/// sprite texture). The audio layer reads the filesystem directly, so resolve
/// through the VFS here, where the mounts are known, and hand it a real path.
/// A path that does not resolve is returned unchanged (a caller may already
/// have given an absolute one).
std::string resolveAudioPath(const std::string& path)
{
    if (VirtualFileSystem* vfs = VirtualFileSystem::get(); vfs != nullptr) {
        return vfs->translatePath(path).string();
    }
    return path;
}

// `audio.play(clip, { bus, volume, pitch, loop })` -> voice handle, or nil when
// there is no device or the clip cannot be loaded. The clip path is project
// relative (resolved by the asset layer before this), the same as spawnSprite's
// texture.
ScriptValue audioPlayFn(const GameplayScriptContext& ctx, ScriptArgs args)
{
    if (args.empty() || args.size() > 2) {
        throw ScriptError("expects audio.play(path) or audio.play(path, { bus, volume, pitch, loop })");
    }
    if (!ctx.audio || !ctx.audio->isAvailable()) {
        return {};
    }
    const std::string path = script::scriptToString(args[0]);
    FAudioPlayOptions options;
    if (args.size() > 1) {
        const FAudioPlayOptions* table = script::scriptIf<FAudioPlayOptions>(args[1]);
        if (table == nullptr) {
            throw ScriptError("expects audio.play(path, { bus, volume, pitch, loop })");
        }
        options = *table;
    }
    auto clip = ctx.audio->loadClip(resolveAudioPath(path));
    if (!clip) {
        return {};
    }
    AudioPlayDesc desc;
    desc.bus    = audioBusNamed(options.bus);
    desc.volume = options.volume;
    desc.pitch  = options.pitch;
    desc.bLoop  = options.bLoop;
    auto voice = ctx.audio->play(clip, desc);
    if (!voice) {
        return {};
    }
    // A voice is only useful to stop it; scripts rarely hold it, so return a
    // truthy token instead of a live handle. `audio.stopAll` / bus volume cover
    // the common cases.
    return script::scriptMake(true);
}

ScriptValue audioMusicFn(const GameplayScriptContext& ctx, ScriptArgs args)
{
    if (args.empty() || args.size() > 2) {
        throw ScriptError("expects audio.music(path[, seconds])");
    }
    if (!ctx.audio || !ctx.audio->isAvailable()) {
        return {};
    }
    const std::string path  = script::scriptToString(args[0]);
    const float       fade  = args.size() > 1 ? static_cast<float>(script::scriptToNumber(args[1])) : 0.0f;
    if (path.empty()) {
        ctx.audio->stopMusic(fade);
        return {};
    }
    auto clip = ctx.audio->loadClip(resolveAudioPath(path));
    if (!clip) {
        return {};
    }
    ctx.audio->playMusic(clip, fade);
    return {};
}

ScriptValue audioStopMusicFn(const GameplayScriptContext& ctx, ScriptArgs args)
{
    if (args.size() > 1) {
        throw ScriptError("expects audio.stopMusic([seconds])");
    }
    if (ctx.audio && ctx.audio->isAvailable()) {
        ctx.audio->stopMusic(args.empty() ? 0.0f : static_cast<float>(script::scriptToNumber(args[0])));
    }
    return {};
}

ScriptValue audioSetBusFn(const GameplayScriptContext& ctx, ScriptArgs args)
{
    expectArgCount(args, 2);
    if (ctx.audio && ctx.audio->isAvailable()) {
        ctx.audio->setBusVolume(audioBusNamed(script::scriptToString(args[0])),
                                static_cast<float>(script::scriptToNumber(args[1])));
    }
    return {};
}

ScriptValue audioStopAllFn(const GameplayScriptContext& ctx, ScriptArgs args)
{
    expectArgCount(args, 0);
    if (ctx.audio && ctx.audio->isAvailable()) {
        ctx.audio->stopAll();
    }
    return {};
}

// `particles.burst(entity[, count])`: spawn a one-shot burst on an entity that
// carries a ParticleEmitterComponent. A missing entity or component is a no-op
// (a scene may not have effects), matching the other gameplay calls.
ScriptValue particlesBurstFn(const GameplayScriptContext& /*ctx*/, ScriptArgs args)
{
    if (args.empty() || args.size() > 2) {
        throw ScriptError("expects particles.burst(entity) or particles.burst(entity, count)");
    }
    const auto* ref = script::scriptIf<ScriptRef>(args[0]);
    if (ref == nullptr) {
        throw ScriptError("expected an entity");
    }
    Entity* entity = script::entityOf(*ref);
    if (!entity) {
        return {};
    }
    ParticleEmitterComponent* emitter = entity->tryGetComponent<ParticleEmitterComponent>();
    if (!emitter) {
        return {};
    }
    const int count = args.size() > 1 ? static_cast<int>(script::scriptToInteger(args[1])) : 8;
    emitter->spawnBurst(count);
    return {};
}

// `particles.setEmitting(entity, on)`: toggle continuous emission on an entity.
ScriptValue particlesSetEmittingFn(const GameplayScriptContext& /*ctx*/, ScriptArgs args)
{
    expectArgCount(args, 2);
    const auto* ref = script::scriptIf<ScriptRef>(args[0]);
    if (ref == nullptr) {
        throw ScriptError("expected an entity");
    }
    if (Entity* entity = script::entityOf(*ref)) {
        if (ParticleEmitterComponent* emitter = entity->tryGetComponent<ParticleEmitterComponent>()) {
            emitter->bEmitting = script::scriptToBool(args[1]);
        }
    }
    return {};
}

// `render.setLook{ ... }`: read-modify-write the pipeline's post-processing
// state. Applied fields override; unset fields keep the current look, so a
// script can raise only exposure without resetting bloom. No renderer (a
// headless run) is a silent no-op so scripts stay runnable in tests.
ScriptValue setLookFn(const GameplayScriptContext& ctx, ScriptArgs args)
{
    expectArgCount(args, 1);
    const FLookOptions* options = script::scriptIf<FLookOptions>(args[0]);
    if (options == nullptr) {
        throw ScriptError("expects setLook{ exposure, bloom, grayscale, kernel, ... }");
    }
    if (!ctx.mutatePostProcessing) {
        return {};
    }
    const FLookOptions in = *options;
    ctx.mutatePostProcessing([in](PostProcessingState& post) {
        post.bEnableToneMapping   = in.bToneMap;
        post.bEnableGammaCorrection = in.bGamma;
        post.exposure             = in.exposure;
        post.bEnableBloom         = in.bBloom;
        post.bloomThreshold       = in.bloomThreshold;
        post.bloomStrength        = in.bloomStrength;
        post.grayscaleMode =
            static_cast<PostProcessingState::EGrayscaleMode>(std::clamp<int64_t>(in.grayscale, 0, 2));
        post.kernelMode = static_cast<PostProcessingState::EKernelMode>(std::clamp<int64_t>(in.kernel, 0, 3));
        post.bEnableRandomGrain = in.bGrain;
        post.bEnableInversion   = in.bInversion;
    });
    return {};
}

ScriptValue viewAspect(const GameplayScriptContext& ctx, ScriptArgs args)
{
    expectArgCount(args, 0);
    const GameplayPresentedView presented = gameplayPresentedView(ctx);
    return script::scriptMake(static_cast<double>(scriptViewAspect(presented.camera, presented.pixelExtent)));
}

ScriptValue scriptOf(const GameplayScriptContext& ctx, ScriptArgs args)
{
    if (args.empty() || args.size() > 2) {
        throw ScriptError("expects scriptOf(entity) or scriptOf(entity, path)");
    }
    if (!(script::scriptIf<ScriptRef>(args[0]) != nullptr)) {
        throw ScriptError("expected an entity");
    }
    Entity* entity = script::entityOf((*script::scriptIf<ScriptRef>(args[0])));
    if (!entity) {
        return {};
    }
    const std::string path = args.size() > 1 ? optionalString(args[1]) : std::string{};
    if (!ctx.worldRuntime) {
        return {};
    }
    IWorldScriptRuntime* runtime = ctx.worldRuntime();
    if (!runtime) {
        return {};
    }
    const script::ScriptSelfHandle self = entityScriptSelf(*runtime, *entity, path);
    if (self.runtimeId == 0) {
        return {};
    }
    return script::scriptMake(self);
}

ScriptValue uiGet(const GameplayScriptContext& ctx, ScriptArgs args)
{
    expectArgCount(args, 1);
    if (!ctx.gameUI) {
        return {};
    }
    const std::string entryId = script::scriptToString(args[0]);
    UIElementRef      root    = ctx.gameUI->findEntryRoot(entryId);
    if (!root) {
        YA_CORE_WARN("ui.get: no mounted Game UI entry '{}'", entryId);
        return {};
    }
    // A loaded entry script answers as its self table. Otherwise the root
    // widget handle, so `.name` still works on an entry that has no script.
    const script::ScriptSelfHandle self = LuaWidgetScripts::scriptSelfOf(*root);
    if (self.runtimeId != 0) {
        return script::scriptMake(self);
    }
    const script::ScriptRef handle = widgetScriptRef(*ctx.gameUI, root);
    if (!handle) {
        return {};
    }
    return script::scriptMake(handle);
}

} // namespace

void placePlayerAtSpawn(Scene& scene, const std::string& spawnName, const std::string& actorName)
{
    Entity* marker = scene.getEntityByName(spawnName);
    if (!marker) {
        YA_CORE_WARN("placePlayerAtSpawn: no spawn marker named '{}'", spawnName);
        return;
    }
    if (actorName.empty()) {
        YA_CORE_WARN("placePlayerAtSpawn: no actor name; the caller names the entity to place");
        return;
    }
    Entity* actor = scene.getEntityByName(actorName);
    if (!actor) {
        YA_CORE_WARN("placePlayerAtSpawn: no entity named '{}' in this scene", actorName);
        return;
    }
    auto* markerTransform = marker->hasComponent<TransformComponent>() ? marker->getComponent<TransformComponent>() : nullptr;
    auto* actorTransform  = actor->hasComponent<TransformComponent>() ? actor->getComponent<TransformComponent>() : nullptr;
    if (!markerTransform || !actorTransform) {
        return;
    }
    // Spawn markers live at scene root, so the local position is the world
    // position; z stays the actor's own (it sets its depth itself).
    const glm::vec3 spawn   = markerTransform->getPosition();
    const glm::vec3 current = actorTransform->getPosition();
    actorTransform->setPosition({spawn.x, spawn.y, current.z});
}

void registerRuntimeScriptNatives()
{
    static bool once = false;
    if (once) {
        return;
    }
    once = true;

    registerWidgetNativeMethods();
    TilemapComponent::__ensure_reflection_registered();
    script::addMethod(type_index_v<TilemapComponent>,
                      "entityAt",
                      script::fnVariadic<const TilemapComponent&, int64_t, int64_t, script::ScriptRef>(&entityAtCell));
    registerEntityScriptCall();
}

void registerGameplayScriptFunctions(GameplayScriptContext ctx)
{
    registerRuntimeScriptNatives();
    script::registerModuleFunction("world", "find", [ctx](ScriptArgs args) { return find(ctx, args); });
    script::registerModuleFunction("world", "viewSize", [ctx](ScriptArgs args) { return viewSize(ctx, args); });
    script::registerModuleFunction("world", "loadScene", [ctx](ScriptArgs args) -> ScriptValue {
        if (args.empty() || args.size() > 3) {
            throw ScriptError("expects loadScene(path), loadScene(path, spawnName), or loadScene(path, spawnName, actorName)");
        }
        if (!ctx.requestSceneTransfer) {
            throw ScriptError("no running app");
        }
        ctx.requestSceneTransfer(script::scriptToString(args[0]),
                                 args.size() > 1 ? script::scriptToString(args[1]) : std::string{},
                                 args.size() > 2 ? script::scriptToString(args[2]) : std::string{});
        return {};
    });
    script::registerModuleFunction("world", "save", [ctx](ScriptArgs args) -> ScriptValue {
        expectArgCount(args, 1);
        if (!ctx.saveWorld) {
            throw ScriptError("no running app");
        }
        if (!ctx.saveWorld(script::scriptToString(args[0]))) {
            throw ScriptError("world.save failed");
        }
        return {};
    });
    script::registerModuleFunction("world", "load", [ctx](ScriptArgs args) -> ScriptValue {
        expectArgCount(args, 1);
        if (!ctx.loadWorld) {
            throw ScriptError("no running app");
        }
        if (!ctx.loadWorld(script::scriptToString(args[0]))) {
            throw ScriptError("world.load failed");
        }
        return {};
    });
    script::registerModuleFunction("world", "saveRestored", [ctx](ScriptArgs args) -> ScriptValue {
        expectArgCount(args, 0);
        if (!ctx.worldRuntime) {
            throw ScriptError("no running app");
        }
        IWorldScriptRuntime* runtime = ctx.worldRuntime();
        return script::scriptMake(runtime != nullptr && runtime->saveRestored());
    });
    script::registerModuleFunction("world", "viewAspect", [ctx](ScriptArgs args) { return viewAspect(ctx, args); });
    script::registerModuleFunction("world", "spawnSprite", [ctx](ScriptArgs args) { return spawnSpriteFn(ctx, args); },
                                   {0, spriteSpawnOptionsType()});
    script::registerModuleFunction("world", "spawnText", [ctx](ScriptArgs args) { return spawnTextFn(ctx, args); },
                                   {0, 0, 0, textSpawnOptionsType()});
    script::registerModuleFunction("render", "setLook", [ctx](ScriptArgs args) { return setLookFn(ctx, args); },
                                   {lookOptionsType()});
    script::registerModuleFunction("audio", "play", [ctx](ScriptArgs args) { return audioPlayFn(ctx, args); },
                                   {0, audioPlayOptionsType()});
    script::registerModuleFunction("audio", "music", [ctx](ScriptArgs args) { return audioMusicFn(ctx, args); });
    script::registerModuleFunction("audio", "stopMusic", [ctx](ScriptArgs args) { return audioStopMusicFn(ctx, args); });
    script::registerModuleFunction("audio", "setBus", [ctx](ScriptArgs args) { return audioSetBusFn(ctx, args); });
    script::registerModuleFunction("audio", "stopAll", [ctx](ScriptArgs args) { return audioStopAllFn(ctx, args); });
    script::registerModuleFunction("particles", "burst", [ctx](ScriptArgs args) { return particlesBurstFn(ctx, args); });
    script::registerModuleFunction("particles", "setEmitting", [ctx](ScriptArgs args) { return particlesSetEmittingFn(ctx, args); });
    script::registerModuleFunction("world", "destroyEntity", [ctx](ScriptArgs args) { return destroyEntityFn(ctx, args); });
    script::registerModuleFunction("world", "scriptOf", [ctx](ScriptArgs args) { return scriptOf(ctx, args); });
    script::registerModuleFunction("ui", "get", [ctx](ScriptArgs args) { return uiGet(ctx, args); });
}

} // namespace ya
