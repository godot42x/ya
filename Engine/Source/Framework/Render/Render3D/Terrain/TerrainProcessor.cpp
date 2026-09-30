#include "Render3D/Terrain/TerrainProcessor.h"

#include "Core/Log.h"
#include "ECS/SceneBus.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "RHI/Render.h"
#include "Render3D/Terrain/TerrainMeshBuilder.h"
#include "Resource/AssetManager.h"
#include "Resource/Mesh.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace ya
{

namespace
{

float terrainHalfToFloat(uint16_t value)
{
    const uint32_t sign  = static_cast<uint32_t>(value & 0x8000u) << 16;
    const uint32_t exp   = static_cast<uint32_t>(value & 0x7C00u);
    const uint32_t mant  = static_cast<uint32_t>(value & 0x03FFu);

    uint32_t result = 0;
    if (exp == 0) {
        result = sign | (mant << 13);
    }
    else if (exp == 0x7C00u) {
        result = sign | 0x7F800000u | (mant << 13);
    }
    else {
        result = sign | ((exp + 0x1C000u) << 13) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &result, sizeof(out));
    return out;
}

std::vector<float> extractTerrainHeights(const AssetManager::TextureMemoryBlock& texture)
{
    std::vector<float> heights;
    if (!texture.isValid() || texture.channels == 0) {
        return heights;
    }

    const size_t pixelCount = static_cast<size_t>(texture.width) * texture.height;
    heights.resize(pixelCount, 0.0f);

    switch (texture.payloadType) {
    case AssetManager::ETexturePayloadType::U8: {
        const auto* data = texture.bytes.data();
        for (size_t i = 0; i < pixelCount; ++i) {
            heights[i] = static_cast<float>(data[i * texture.channels]) / 255.0f;
        }
        break;
    }
    case AssetManager::ETexturePayloadType::F16: {
        const auto* data = reinterpret_cast<const uint16_t*>(texture.bytes.data());
        for (size_t i = 0; i < pixelCount; ++i) {
            heights[i] = terrainHalfToFloat(data[i * texture.channels]);
        }
        break;
    }
    case AssetManager::ETexturePayloadType::F32: {
        const auto* data = reinterpret_cast<const float*>(texture.bytes.data());
        for (size_t i = 0; i < pixelCount; ++i) {
            heights[i] = data[i * texture.channels];
        }
        break;
    }
    default:
        heights.clear();
        break;
    }

    for (auto& height : heights) {
        height = std::clamp(height, 0.0f, 1.0f);
    }
    return heights;
}

std::string buildTerrainDerivedKey(const TerrainComponent& terrain, uint64_t heightMapVersion)
{
    return std::format("terrain|{}|{}|{:.6f}|{:.6f}|{:.6f}|{}|{}",
                       AssetManager::normalizeAssetPath(terrain._heightMapRef.getPath()),
                       heightMapVersion,
                       terrain._size.x,
                       terrain._size.y,
                       terrain._heightScale,
                       terrain._heightOffset,
                       terrain._gridResolution);
}

} // namespace

void TerrainProcessor::init()
{
    if (bBusSubscribed) {
        return;
    }
    SceneBus& bus = SceneBus::get();
    _componentAddedHandle =
        bus.onComponentAdded.addLambda([this](entt::registry& registry, entt::entity entity, ya::type_index_t type) {
            onComponentAdded(registry, entity, type);
        });
    _componentEditedHandle =
        bus.onComponentEdited.addLambda([this](entt::registry& registry, entt::entity entity, ya::type_index_t type) {
            onComponentEdited(registry, entity, type);
        });
    _componentRemovedHandle =
        bus.onComponentRemoved.addLambda([this](entt::registry& registry, entt::entity entity, ya::type_index_t type) {
            onComponentRemoved(registry, entity, type);
        });
    bBusSubscribed = true;
}

void TerrainProcessor::shutdown()
{
    if (bBusSubscribed) {
        SceneBus& bus = SceneBus::get();
        bus.onComponentAdded.remove(_componentAddedHandle);
        bus.onComponentEdited.remove(_componentEditedHandle);
        bus.onComponentRemoved.remove(_componentRemovedHandle);
        _componentAddedHandle = _componentEditedHandle = _componentRemovedHandle = INVALID_HANDLE;
        bBusSubscribed = false;
    }
    clearPendingResolveStates();
}

void TerrainProcessor::clearPendingResolveStates()
{
    for (auto& [scene, work] : _sceneWork) {
        (void)scene;
        dropWork(work);
    }
    _sceneWork.clear();
    _terrainDerivedResources.clear();
}

void TerrainProcessor::prepareScenes(std::span<Scene* const> scenes, float dt)
{
    YA_PROFILE_FUNCTION();

    (void)dt;

    // A Scene this tick does not prepare is not one of the Scenes the last
    // tick left behind, so its work goes here. Preparing one Scene can
    // therefore never take another Scene's work with it.
    dropScenesAbsentFrom(scenes);

    for (Scene* scene : scenes) {
        if (!scene) {
            continue;
        }

        SceneWork& work = ensureWork(*scene);
        if (!work.bSeeded) {
            work.bSeeded = true;
            seedSceneResolveWork(work);
        }

        promoteDueRebuilds(work);
        gcDerivedResources(currentHostTick());
        resolvePendingTerrain(work);
    }
}

TerrainProcessor::SceneWork& TerrainProcessor::ensureWork(Scene& scene)
{
    SceneWork& work = _sceneWork[&scene];
    work.scene      = &scene;
    return work;
}

const TerrainProcessor::SceneWork* TerrainProcessor::findWork(const Scene& scene) const
{
    const auto it = _sceneWork.find(&scene);
    return it == _sceneWork.end() ? nullptr : &it->second;
}

TerrainProcessor::SceneWork* TerrainProcessor::findWorkByRegistry(const entt::registry* registry)
{
    if (!registry) {
        return nullptr;
    }
    for (auto& [scene, work] : _sceneWork) {
        if (scene && &scene->getRegistry() == registry) {
            return &work;
        }
    }
    return nullptr;
}

void TerrainProcessor::dropScenesAbsentFrom(std::span<Scene* const> scenes)
{
    for (auto it = _sceneWork.begin(); it != _sceneWork.end();) {
        if (std::ranges::find(scenes, it->second.scene) != scenes.end()) {
            ++it;
            continue;
        }
        dropWork(it->second);
        it = _sceneWork.erase(it);
    }
}

void TerrainProcessor::dropWork(SceneWork& work)
{
    // Dropping tracks unsubscribes slot observers and discards in-flight batches.
    std::vector<entt::entity> entities;
    entities.reserve(work.tracks.size() + work.states.size());
    for (const auto& [entity, unused] : work.tracks) {
        (void)unused;
        entities.push_back(entity);
    }
    for (const auto& [entity, unused] : work.states) {
        (void)unused;
        entities.push_back(entity);
    }
    for (const auto entity : entities) {
        cleanupTerrainState(work, entity);
    }
    work.states.clear();
    work.tracks.clear();
    work.dirtyQueue.clear();
    work.dirtySet.clear();
    work.debounce = {};
}

void TerrainProcessor::seedSceneResolveWork(SceneWork& work)
{
    auto& registry = work.scene->getRegistry();
    for (auto&& [entity, terrain] : registry.view<TerrainComponent>().each()) {
        scheduleRebuild(work, entity, terrain.getRebuildNotBeforeTick(), "scene seed");
    }
}

void TerrainProcessor::promoteDueRebuilds(SceneWork& work)
{
    const uint64_t now = currentHostTick();
    auto&          registry = work.scene->getRegistry();
    while (!work.debounce.empty() && work.debounce.top().tick <= now) {
        const auto entry = work.debounce.top();
        work.debounce.pop();

        const auto track = work.tracks.find(entry.entity);
        if (track == work.tracks.end() || track->second.scheduleSerial != entry.serial) {
            continue;
        }
        if (!registry.valid(entry.entity) || !registry.all_of<TerrainComponent>(entry.entity)) {
            cleanupTerrainState(work, entry.entity);
            continue;
        }
        enqueueTerrain(work, entry.entity);
    }
}

void TerrainProcessor::gcDerivedResources(uint64_t currentTick)
{
    const auto shouldKeep = [currentTick](uint64_t lastUsedTick) {
        return lastUsedTick + DERIVED_RESOURCE_GC_DELAY_TICKS > currentTick;
    };

    for (auto it = _terrainDerivedResources.begin(); it != _terrainDerivedResources.end();) {
        if (!it->second || shouldKeep(it->second->lastUsedTick)) {
            ++it;
            continue;
        }
        it = _terrainDerivedResources.erase(it);
    }
}

void TerrainProcessor::cleanupTerrainState(SceneWork& work, entt::entity entity)
{
    if (const auto track = work.tracks.find(entity); track != work.tracks.end()) {
        if (track->second.inflightBatchHandle != 0) {
            if (auto* assets = AssetManager::get()) {
                AssetManager::TextureBatchMemory discarded;
                assets->consumeTextureBatchMemory(track->second.inflightBatchHandle, discarded);
            }
        }
        work.tracks.erase(track);
    }
    work.states.erase(entity);
    work.dirtySet.erase(entity);
    std::erase(work.dirtyQueue, entity);
}

void TerrainProcessor::cancelInflightBatch(SceneWork& work, entt::entity entity)
{
    auto&      track  = work.tracks[entity];
    const auto handle = track.inflightBatchHandle;
    ++track.batchSerial;
    track.inflightBatchHandle = 0;
    if (const auto state = work.states.find(entity); state != work.states.end()) {
        state->second.pendingHeightMapHandle = 0;
    }
    if (handle == 0) {
        return;
    }
    if (auto* assets = AssetManager::get()) {
        AssetManager::TextureBatchMemory discarded;
        assets->consumeTextureBatchMemory(handle, discarded);
    }
}

void TerrainProcessor::scheduleRebuild(SceneWork&            work,
                                       entt::entity          entity,
                                       uint64_t              rebuildNotBeforeTick,
                                       const char*           reason)
{
    auto& registry = work.scene->getRegistry();
    if (!registry.valid(entity) || !registry.all_of<TerrainComponent>(entity)) {
        cleanupTerrainState(work, entity);
        return;
    }

    auto& terrain = registry.get<TerrainComponent>(entity);
    if (rebuildNotBeforeTick > terrain.getRebuildNotBeforeTick()) {
        terrain.setRebuildNotBeforeTick(rebuildNotBeforeTick);
    }

    cancelInflightBatch(work, entity);

    auto& state = work.states[entity];
    auto& track = work.tracks[entity];
    state.state = terrain.hasHeightMap() ? TerrainRuntimeState::EResolveState::Dirty
                                          : TerrainRuntimeState::EResolveState::Empty;
    state.pendingHeightMapHandle     = 0;
    state.lastQueuedAuthoringVersion = terrain.getAuthoringVersion();
    state.lastDirtyReason            = reason ? reason : "dirty";

    work.dirtySet.erase(entity);
    std::erase(work.dirtyQueue, entity);

    ++track.scheduleSerial;
    const uint64_t now = currentHostTick();
    const uint64_t due = rebuildNotBeforeTick > now ? rebuildNotBeforeTick : now;
    track.scheduledTick = due;

    if (due > now) {
        // A future tick owns this rebuild. Drop the slot subscription so a
        // height-map fill cannot jump the heap.
        track.heightMapToken.reset();
        track.heightMapHandle.reset();
        work.debounce.push(SceneWork::DeferredRebuild{due, entity, track.scheduleSerial});
        return;
    }

    enqueueTerrain(work, entity);
}

void TerrainProcessor::holdHeightMapSlot(SceneWork& work, entt::entity entity, const AssetHandle<Texture>& handle)
{
    auto& track = work.tracks[entity];
    if (track.heightMapHandle == handle && track.heightMapToken.valid()) {
        return;
    }
    track.heightMapToken.reset();
    track.heightMapHandle = handle;
    if (!handle) {
        return;
    }
    const entt::registry* registry = &work.scene->getRegistry();
    track.heightMapToken = handle->observers.subscribe([this, registry, entity]() {
        // Slot fills land on the game thread; the callback only enqueues.
        enqueueFromHeightMapSlot(registry, entity);
    });
}

void TerrainProcessor::enqueueTerrain(SceneWork& work, entt::entity entity)
{
    if (work.dirtySet.insert(entity).second) {
        work.dirtyQueue.push_back(entity);
    }
}

void TerrainProcessor::enqueueFromHeightMapSlot(const entt::registry* registry, entt::entity entity)
{
    SceneWork* work = findWorkByRegistry(registry);
    if (!work) {
        return;
    }
    const auto track = work->tracks.find(entity);
    if (track == work->tracks.end() || track->second.scheduledTick > currentHostTick()) {
        return;
    }
    enqueueTerrain(*work, entity);
}

void TerrainProcessor::onHeightMapBatch(const entt::registry* registry,
                                        entt::entity          entity,
                                        uint64_t              serial,
                                        uint64_t              handle)
{
    auto discard = [&]() {
        if (handle == 0) {
            return;
        }
        if (auto* assets = AssetManager::get()) {
            AssetManager::TextureBatchMemory discarded;
            assets->consumeTextureBatchMemory(handle, discarded);
        }
    };

    SceneWork* work = findWorkByRegistry(registry);
    if (!work) {
        discard();
        return;
    }
    const auto track = work->tracks.find(entity);
    if (track == work->tracks.end() || track->second.batchSerial != serial) {
        discard();
        return;
    }
    enqueueTerrain(*work, entity);
}

void TerrainProcessor::onComponentAdded(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex)
{
    if (typeIndex != type_index_v<TerrainComponent>) {
        return;
    }
    SceneWork* work = findWorkByRegistry(&registry);
    if (!work || !registry.valid(entity) || !registry.all_of<TerrainComponent>(entity)) {
        return;
    }
    auto& terrain = registry.get<TerrainComponent>(entity);
    scheduleRebuild(*work, entity, terrain.getRebuildNotBeforeTick(), "component added");
}

void TerrainProcessor::onComponentEdited(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex)
{
    if (typeIndex != type_index_v<TerrainComponent>) {
        return;
    }
    SceneWork* work = findWorkByRegistry(&registry);
    if (!work || !registry.valid(entity) || !registry.all_of<TerrainComponent>(entity)) {
        return;
    }
    auto& terrain = registry.get<TerrainComponent>(entity);
    scheduleRebuild(*work, entity, terrain.getRebuildNotBeforeTick(), "component edited");
}

void TerrainProcessor::onComponentRemoved(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex)
{
    if (typeIndex != type_index_v<TerrainComponent>) {
        return;
    }
    if (SceneWork* work = findWorkByRegistry(&registry)) {
        cleanupTerrainState(*work, entity);
    }
}

Mesh* TerrainProcessor::getTerrainMesh(const Scene& scene, entt::entity entity) const
{
    const SceneWork* work = findWork(scene);
    if (!work) {
        return nullptr;
    }
    const auto it = work->states.find(entity);
    if (it == work->states.end() || !it->second.boundResource) {
        return nullptr;
    }
    return it->second.boundResource->mesh.get();
}

const TerrainRuntimeState* TerrainProcessor::findTerrainState(const Scene& scene, entt::entity entity) const
{
    const SceneWork* work = findWork(scene);
    if (!work) {
        return nullptr;
    }
    const auto it = work->states.find(entity);
    return it == work->states.end() ? nullptr : &it->second;
}

void TerrainProcessor::resolvePendingTerrain(SceneWork& work)
{
    auto& registry = work.scene->getRegistry();
    auto* assets   = AssetManager::get();
    if (!assets) {
        return;
    }

    auto pumpOne = [&](entt::entity entity) {
        if (!registry.valid(entity) || !registry.all_of<TerrainComponent>(entity)) {
            cleanupTerrainState(work, entity);
            return;
        }

        auto& terrain = registry.get<TerrainComponent>(entity);
        auto& state   = work.states[entity];
        auto& track   = work.tracks[entity];
        const uint64_t currentTick = this->currentHostTick();

        if (!terrain.hasHeightMap()) {
            track.heightMapToken.reset();
            track.heightMapHandle.reset();
            state.state                         = TerrainRuntimeState::EResolveState::Empty;
            state.pendingHeightMapHandle        = 0;
            track.inflightBatchHandle           = 0;
            state.lastBuiltHeightMapVersion     = 0;
            state.currentDerivedKey.clear();
            state.boundResource.reset();
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            return;
        }

        holdHeightMapSlot(work, entity, terrain._heightMapRef._handle);

        const uint64_t    heightMapVersion = assets->getResourceVersion(terrain._heightMapRef.getPath());
        const std::string derivedKey       = buildTerrainDerivedKey(terrain, heightMapVersion);
        if (!state.currentDerivedKey.empty() &&
            state.currentDerivedKey != derivedKey &&
            state.state == TerrainRuntimeState::EResolveState::Ready) {
            state.state = TerrainRuntimeState::EResolveState::Dirty;
        }

        if (auto it = _terrainDerivedResources.find(derivedKey); it != _terrainDerivedResources.end() &&
            it->second && it->second->mesh) {
            it->second->lastUsedTick            = currentTick;
            state.currentDerivedKey             = derivedKey;
            state.boundResource                 = it->second;
            state.lastBuiltHeightMapVersion     = it->second->heightMapVersion;
            state.pendingHeightMapHandle        = 0;
            track.inflightBatchHandle           = 0;
            state.state                         = TerrainRuntimeState::EResolveState::Ready;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            return;
        }

        if (state.state == TerrainRuntimeState::EResolveState::Ready &&
            state.lastBuiltHeightMapVersion != heightMapVersion) {
            state.state = TerrainRuntimeState::EResolveState::Dirty;
        }

        if (state.state != TerrainRuntimeState::EResolveState::Dirty &&
            state.state != TerrainRuntimeState::EResolveState::LoadingHeightMap) {
            state.currentDerivedKey             = derivedKey;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            return;
        }

        if (state.pendingHeightMapHandle == 0) {
            const uint64_t            serial   = ++track.batchSerial;
            const entt::registry*     registry = &work.scene->getRegistry();
            const auto handle = assets->loadTextureBatchIntoMemory(AssetManager::TextureBatchMemoryLoadRequest{
                .filepaths  = {terrain._heightMapRef.getPath()},
                .colorSpace = AssetManager::ETextureColorSpace::Linear,
                .onReady    = [this, registry, entity, serial](uint64_t batchHandle) {
                    onHeightMapBatch(registry, entity, serial, batchHandle);
                },
            });
            state.pendingHeightMapHandle     = handle;
            track.inflightBatchHandle        = handle;
            track.startedResourceVersion     = heightMapVersion;
            state.state                       = TerrainRuntimeState::EResolveState::LoadingHeightMap;
            state.lastStartedAuthoringVersion = terrain.getAuthoringVersion();
            return;
        }

        AssetManager::TextureBatchMemory batchMemory;
        if (!assets->consumeTextureBatchMemory(state.pendingHeightMapHandle, batchMemory)) {
            // The batch callback enqueues this entity when the decode lands.
            // A second prepare before that must not spin.
            return;
        }
        state.pendingHeightMapHandle = 0;
        track.inflightBatchHandle    = 0;

        if (!batchMemory.isValid() || batchMemory.textures.empty()) {
            YA_CORE_WARN("Terrain height map decode failed: {}", terrain._heightMapRef.getPath());
            state.state                         = TerrainRuntimeState::EResolveState::Failed;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            return;
        }

        const auto& texture = batchMemory.textures.front();
        if (AssetManager::normalizeAssetPath(texture.filepath) != AssetManager::normalizeAssetPath(terrain._heightMapRef.getPath())) {
            scheduleRebuild(work, entity, terrain.getRebuildNotBeforeTick(), "terrain stale async result");
            return;
        }

        auto heights = extractTerrainHeights(texture);
        if (heights.empty()) {
            YA_CORE_WARN("Terrain height map has unsupported payload: {}", terrain._heightMapRef.getPath());
            state.state                         = TerrainRuntimeState::EResolveState::Failed;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            return;
        }

        auto meshData = buildTerrainMeshData(TerrainMeshBuildDesc{
            .name           = std::format("terrain_{}", terrain._heightMapRef.getPath()),
            .size           = terrain._size,
            .heightScale    = terrain._heightScale,
            .heightOffset   = terrain._heightOffset,
            .gridResolution = terrain._gridResolution,
            .heightWidth    = texture.width,
            .heightHeight   = texture.height,
            .heights        = heights,
        });

        auto  resource = std::make_shared<TerrainDerivedResource>();
        auto* render   = getRender();
        YA_CORE_ASSERT(render, "TerrainProcessor mesh creation requires render backend");
        resource->mesh                          = Mesh::create(*render, meshData);
        resource->heightMapVersion              = heightMapVersion;
        resource->lastUsedTick                  = currentTick;
        _terrainDerivedResources[derivedKey]    = resource;

        state.currentDerivedKey             = derivedKey;
        state.boundResource                 = resource;
        state.pendingHeightMapHandle        = 0;
        state.lastBuiltHeightMapVersion     = heightMapVersion;
        state.state                         = TerrainRuntimeState::EResolveState::Ready;
        state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
    };

    while (!work.dirtyQueue.empty()) {
        const auto entity = work.dirtyQueue.front();
        work.dirtyQueue.pop_front();
        work.dirtySet.erase(entity);
        pumpOne(entity);
    }
}

} // namespace ya
