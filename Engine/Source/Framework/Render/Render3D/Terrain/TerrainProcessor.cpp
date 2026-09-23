#include "Render3D/Terrain/TerrainProcessor.h"

#include "Core/Log.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "RHI/Render.h"
#include "Render3D/Terrain/TerrainMeshBuilder.h"
#include "Resource/AssetManager.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
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

void TerrainProcessor::shutdown()
{
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

        sweepAuthoringDirty(work);
        auditResolveWork(work);
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
    for (auto& [entity, state] : work.states) {
        (void)entity;
        state = TerrainRuntimeState{};
    }
    work.states.clear();
    work.dirtyQueue.clear();
    work.dirtySet.clear();
    work.active.clear();
    work.nextResolveAuditTick = 0;
}

void TerrainProcessor::seedSceneResolveWork(SceneWork& work)
{
    auto& registry = work.scene->getRegistry();
    for (auto&& [entity, terrain] : registry.view<TerrainComponent>().each()) {
        markTerrainDirty(work, entity, "scene seed", terrain.getRebuildNotBeforeTick());
    }
}

bool TerrainProcessor::isTerrainQueuedOrActive(const SceneWork& work, entt::entity entity) const
{
    return work.dirtySet.contains(entity) || work.active.contains(entity);
}

void TerrainProcessor::sweepAuthoringDirty(SceneWork& work)
{
    auto& registry = work.scene->getRegistry();
    for (auto&& [entity, terrain] : registry.view<TerrainComponent>().each()) {
        auto& state = work.states[entity];
        if (terrain.getAuthoringVersion() > state.lastCompletedAuthoringVersion &&
            !isTerrainQueuedOrActive(work, entity)) {
            markTerrainDirty(work, entity, "authoring-version sweep", terrain.getRebuildNotBeforeTick());
        }
    }
}

void TerrainProcessor::auditResolveWork(SceneWork& work)
{
    const uint64_t currentTick = this->currentHostTick();
    if (work.nextResolveAuditTick != 0 && currentTick < work.nextResolveAuditTick) {
        return;
    }
    work.nextResolveAuditTick = currentTick + 120;

    auto& registry = work.scene->getRegistry();
    auto* assets   = AssetManager::get();

    for (auto&& [entity, terrain] : registry.view<TerrainComponent>().each()) {
        auto& state = work.states[entity];
        const bool bVersionNotCompleted = terrain.getAuthoringVersion() > state.lastCompletedAuthoringVersion;
        bool       bHeightMapStale      = false;
        if (assets && terrain.hasHeightMap() &&
            state.state == TerrainRuntimeState::EResolveState::Ready) {
            bHeightMapStale = state.lastBuiltHeightMapVersion !=
                              assets->getResourceVersion(terrain._heightMapRef.getPath());
        }

        if ((bVersionNotCompleted || bHeightMapStale) && !isTerrainQueuedOrActive(work, entity)) {
            YA_CORE_WARN("ResourceResolve audit re-queued Terrain entity {}: completedVersion={}, authoringVersion={}, stale={}",
                         static_cast<uint32_t>(entity),
                         state.lastCompletedAuthoringVersion,
                         terrain.getAuthoringVersion(),
                         bHeightMapStale);
            markTerrainDirty(work, entity, bHeightMapStale ? "audit: height map stale" : "audit: missed terrain enqueue",
                             terrain.getRebuildNotBeforeTick());
        }
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
    work.states.erase(entity);
    work.dirtySet.erase(entity);
    work.active.erase(entity);
    std::erase(work.dirtyQueue, entity);
}

void TerrainProcessor::markTerrainDirty(SceneWork& work, entt::entity entity, const char* reason, uint64_t rebuildNotBeforeTick)
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

    auto& state = work.states[entity];
    state.state                      = terrain.hasHeightMap() ? TerrainRuntimeState::EResolveState::Dirty
                                                              : TerrainRuntimeState::EResolveState::Empty;
    state.pendingHeightMapHandle     = 0;
    state.lastQueuedAuthoringVersion = terrain.getAuthoringVersion();
    state.lastDirtyReason            = reason ? reason : "dirty";
    if (work.dirtySet.insert(entity).second) {
        work.dirtyQueue.push_back(entity);
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
        const uint64_t currentTick = this->currentHostTick();

        if (terrain.getRebuildNotBeforeTick() > currentTick) {
            work.active.insert(entity);
            return;
        }

        if (!terrain.hasHeightMap()) {
            state.state = TerrainRuntimeState::EResolveState::Empty;
            state.pendingHeightMapHandle = 0;
            state.lastBuiltHeightMapVersion = 0;
            state.currentDerivedKey.clear();
            state.boundResource.reset();
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            work.active.erase(entity);
            return;
        }

        const uint64_t heightMapVersion = assets->getResourceVersion(terrain._heightMapRef.getPath());
        const std::string derivedKey = buildTerrainDerivedKey(terrain, heightMapVersion);
        if (!state.currentDerivedKey.empty() &&
            state.currentDerivedKey != derivedKey &&
            state.state == TerrainRuntimeState::EResolveState::Ready) {
            state.state = TerrainRuntimeState::EResolveState::Dirty;
        }

        if (auto it = _terrainDerivedResources.find(derivedKey); it != _terrainDerivedResources.end() &&
            it->second && it->second->mesh) {
            it->second->lastUsedTick  = currentTick;
            state.currentDerivedKey   = derivedKey;
            state.boundResource       = it->second;
            state.lastBuiltHeightMapVersion = it->second->heightMapVersion;
            state.pendingHeightMapHandle    = 0;
            state.state                     = TerrainRuntimeState::EResolveState::Ready;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            work.active.erase(entity);
            return;
        }

        if (state.state == TerrainRuntimeState::EResolveState::Ready &&
            state.lastBuiltHeightMapVersion != heightMapVersion) {
            state.state = TerrainRuntimeState::EResolveState::Dirty;
        }

        if (state.state != TerrainRuntimeState::EResolveState::Dirty &&
            state.state != TerrainRuntimeState::EResolveState::LoadingHeightMap) {
            state.currentDerivedKey = derivedKey;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            work.active.erase(entity);
            return;
        }

        if (state.pendingHeightMapHandle == 0) {
            const auto handle = assets->loadTextureBatchIntoMemory(AssetManager::TextureBatchMemoryLoadRequest{
                .filepaths   = {terrain._heightMapRef.getPath()},
                .colorSpace  = AssetManager::ETextureColorSpace::Linear,
            });
            state.pendingHeightMapHandle = handle;
            state.state                  = TerrainRuntimeState::EResolveState::LoadingHeightMap;
            state.lastStartedAuthoringVersion = terrain.getAuthoringVersion();
            work.active.insert(entity);
            return;
        }

        AssetManager::TextureBatchMemory batchMemory;
        if (!assets->consumeTextureBatchMemory(state.pendingHeightMapHandle, batchMemory)) {
            work.active.insert(entity);
            return;
        }
        state.pendingHeightMapHandle = 0;

        if (!batchMemory.isValid() || batchMemory.textures.empty()) {
            YA_CORE_WARN("Terrain height map decode failed: {}", terrain._heightMapRef.getPath());
            state.state = TerrainRuntimeState::EResolveState::Failed;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            work.active.erase(entity);
            return;
        }

        const auto& texture = batchMemory.textures.front();
        if (AssetManager::normalizeAssetPath(texture.filepath) != AssetManager::normalizeAssetPath(terrain._heightMapRef.getPath())) {
            state.state = TerrainRuntimeState::EResolveState::Dirty;
            markTerrainDirty(work, entity, "terrain stale async result", terrain.getRebuildNotBeforeTick());
            return;
        }

        auto heights = extractTerrainHeights(texture);
        if (heights.empty()) {
            YA_CORE_WARN("Terrain height map has unsupported payload: {}", terrain._heightMapRef.getPath());
            state.state = TerrainRuntimeState::EResolveState::Failed;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            work.active.erase(entity);
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

        auto resource            = std::make_shared<TerrainDerivedResource>();
        auto* render = getRender();
        YA_CORE_ASSERT(render, "TerrainProcessor mesh creation requires render backend");
        resource->mesh           = Mesh::create(*render, meshData);
        resource->heightMapVersion = heightMapVersion;
        resource->lastUsedTick   = currentTick;
        _terrainDerivedResources[derivedKey] = resource;

        state.currentDerivedKey  = derivedKey;
        state.boundResource      = resource;
        state.pendingHeightMapHandle   = 0;
        state.lastBuiltHeightMapVersion = heightMapVersion;
        state.state                    = TerrainRuntimeState::EResolveState::Ready;
        state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
        work.active.erase(entity);
    };

    while (!work.dirtyQueue.empty()) {
        const auto entity = work.dirtyQueue.front();
        work.dirtyQueue.pop_front();
        work.dirtySet.erase(entity);
        pumpOne(entity);
    }

    // Re-pump in-flight (active) terrains every frame so a pending height-map
    // batch decode can be consumed once it completes. Without this pass a
    // terrain left in LoadingHeightMap is never revisited: the audit skips
    // active entities and the queue is empty, so the mesh is never built.
    std::vector<entt::entity> activeEntities(work.active.begin(), work.active.end());
    for (const auto entity : activeEntities) {
        pumpOne(entity);
    }
}

} // namespace ya
