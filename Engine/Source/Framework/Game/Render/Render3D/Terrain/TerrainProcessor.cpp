#include "Render3D/Terrain/TerrainProcessor.h"

#include "Core/Log.h"
#include "Gameplay/Systems/Components/TerrainComponent.h"
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

void TerrainProcessor::init()
{
}

void TerrainProcessor::onUpdate(float dt)
{
    YA_PROFILE_FUNCTION();

    (void)dt;

    auto* const scene = _getActiveScene ? _getActiveScene() : nullptr;
    if (!scene) {
        clearSceneResolveWork();
        return;
    }

    if (_pendingStateScene != scene) {
        clearSceneResolveWork();
        _pendingStateScene = scene;
        seedSceneResolveWork(scene);
    }

    auditResolveWork(scene);
    gcDerivedResources(currentFrame());
    resolvePendingTerrain(scene);
}

void TerrainProcessor::shutdown()
{
    clearPendingResolveStates();
}

void TerrainProcessor::clearSceneResolveWork()
{
    for (auto& [entity, pendingState] : _terrainStates) {
        (void)entity;
        pendingState = TerrainRuntimeState{};
    }
    _terrainStates.clear();
    _dirtyTerrainQueue.clear();
    _dirtyTerrainSet.clear();
    _activeTerrain.clear();
    _nextResolveAuditFrame = 0;
    _pendingStateScene = nullptr;
}

void TerrainProcessor::clearPendingResolveStates()
{
    clearSceneResolveWork();
    _terrainDerivedResources.clear();
}

void TerrainProcessor::seedSceneResolveWork(Scene* scene)
{
    if (!scene) {
        return;
    }

    auto& registry = scene->getRegistry();
    for (auto&& [entity, terrain] : registry.view<TerrainComponent>().each()) {
        (void)terrain;
        markTerrainDirty(entity, "scene seed", terrain.getRebuildNotBeforeFrame());
    }
}

bool TerrainProcessor::isTerrainQueuedOrActive(entt::entity entity) const
{
    return _dirtyTerrainSet.contains(entity) || _activeTerrain.contains(entity);
}

void TerrainProcessor::auditResolveWork(Scene* scene)
{
    if (!scene) {
        return;
    }

    const uint64_t currentFrame = this->currentFrame();
    if (_nextResolveAuditFrame != 0 && currentFrame < _nextResolveAuditFrame) {
        return;
    }
    _nextResolveAuditFrame = currentFrame + 120;

    auto& registry = scene->getRegistry();
    auto* assets   = AssetManager::get();

    for (auto&& [entity, terrain] : registry.view<TerrainComponent>().each()) {
        auto& state = _terrainStates[entity];
        const bool bVersionNotCompleted = terrain.getAuthoringVersion() > state.lastCompletedAuthoringVersion;
        bool       bHeightMapStale      = false;
        if (assets && terrain.hasHeightMap() &&
            state.state == TerrainRuntimeState::EResolveState::Ready) {
            bHeightMapStale = state.lastBuiltHeightMapVersion !=
                              assets->getResourceVersion(terrain._heightMapRef.getPath());
        }

        if ((bVersionNotCompleted || bHeightMapStale) && !isTerrainQueuedOrActive(entity)) {
            YA_CORE_WARN("ResourceResolve audit re-queued Terrain entity {}: completedVersion={}, authoringVersion={}, stale={}",
                         static_cast<uint32_t>(entity),
                         state.lastCompletedAuthoringVersion,
                         terrain.getAuthoringVersion(),
                         bHeightMapStale);
            markTerrainDirty(entity, bHeightMapStale ? "audit: height map stale" : "audit: missed terrain enqueue",
                             terrain.getRebuildNotBeforeFrame());
        }
    }
}

void TerrainProcessor::gcDerivedResources(uint64_t currentFrame)
{
    const auto shouldKeep = [currentFrame](uint64_t lastUsedFrame) {
        return lastUsedFrame + DERIVED_RESOURCE_GC_DELAY_FRAMES > currentFrame;
    };

    for (auto it = _terrainDerivedResources.begin(); it != _terrainDerivedResources.end();) {
        if (!it->second || shouldKeep(it->second->lastUsedFrame)) {
            ++it;
            continue;
        }
        it = _terrainDerivedResources.erase(it);
    }
}

void TerrainProcessor::cleanupTerrainState(entt::entity entity)
{
    _terrainStates.erase(entity);
    _dirtyTerrainSet.erase(entity);
    _activeTerrain.erase(entity);
    std::erase(_dirtyTerrainQueue, entity);
}

void TerrainProcessor::markTerrainDirty(entt::entity entity, const char* reason, uint64_t rebuildNotBeforeFrame)
{
    if (!_pendingStateScene) {
        return;
    }

    auto& registry = _pendingStateScene->getRegistry();
    if (!registry.valid(entity) || !registry.all_of<TerrainComponent>(entity)) {
        cleanupTerrainState(entity);
        return;
    }

    auto& terrain = registry.get<TerrainComponent>(entity);
    if (rebuildNotBeforeFrame > terrain.getRebuildNotBeforeFrame()) {
        terrain.setRebuildNotBeforeFrame(rebuildNotBeforeFrame);
    }

    auto& state = _terrainStates[entity];
    state.state                      = terrain.hasHeightMap() ? TerrainRuntimeState::EResolveState::Dirty
                                                              : TerrainRuntimeState::EResolveState::Empty;
    state.pendingHeightMapHandle     = 0;
    state.lastQueuedAuthoringVersion = terrain.getAuthoringVersion();
    state.lastDirtyReason            = reason ? reason : "dirty";
    if (_dirtyTerrainSet.insert(entity).second) {
        _dirtyTerrainQueue.push_back(entity);
    }
}

Mesh* TerrainProcessor::getTerrainMesh(entt::entity entity) const
{
    const auto it = _terrainStates.find(entity);
    if (it == _terrainStates.end() || !it->second.boundResource) {
        return nullptr;
    }
    return it->second.boundResource->mesh.get();
}

const TerrainRuntimeState* TerrainProcessor::findTerrainState(entt::entity entity) const
{
    const auto it = _terrainStates.find(entity);
    return it == _terrainStates.end() ? nullptr : &it->second;
}

void TerrainProcessor::resolvePendingTerrain(Scene* scene)
{
    auto& registry = scene->getRegistry();
    auto* assets   = AssetManager::get();
    if (!assets) {
        return;
    }

    auto pumpOne = [&](entt::entity entity) {
        if (!registry.valid(entity) || !registry.all_of<TerrainComponent>(entity)) {
            cleanupTerrainState(entity);
            return;
        }

        auto& terrain = registry.get<TerrainComponent>(entity);
        auto& state   = _terrainStates[entity];
        const uint64_t currentFrame = this->currentFrame();

        if (terrain.getRebuildNotBeforeFrame() > currentFrame) {
            _activeTerrain.insert(entity);
            return;
        }

        if (!terrain.hasHeightMap()) {
            state.state = TerrainRuntimeState::EResolveState::Empty;
            state.pendingHeightMapHandle = 0;
            state.lastBuiltHeightMapVersion = 0;
            state.currentDerivedKey.clear();
            state.boundResource.reset();
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.erase(entity);
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
            it->second->lastUsedFrame = currentFrame;
            state.currentDerivedKey   = derivedKey;
            state.boundResource       = it->second;
            state.lastBuiltHeightMapVersion = it->second->heightMapVersion;
            state.pendingHeightMapHandle    = 0;
            state.state                     = TerrainRuntimeState::EResolveState::Ready;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.erase(entity);
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
            _activeTerrain.erase(entity);
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
            _activeTerrain.insert(entity);
            return;
        }

        AssetManager::TextureBatchMemory batchMemory;
        if (!assets->consumeTextureBatchMemory(state.pendingHeightMapHandle, batchMemory)) {
            _activeTerrain.insert(entity);
            return;
        }
        state.pendingHeightMapHandle = 0;

        if (!batchMemory.isValid() || batchMemory.textures.empty()) {
            YA_CORE_WARN("Terrain height map decode failed: {}", terrain._heightMapRef.getPath());
            state.state = TerrainRuntimeState::EResolveState::Failed;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.erase(entity);
            return;
        }

        const auto& texture = batchMemory.textures.front();
        if (AssetManager::normalizeAssetPath(texture.filepath) != AssetManager::normalizeAssetPath(terrain._heightMapRef.getPath())) {
            state.state = TerrainRuntimeState::EResolveState::Dirty;
            markTerrainDirty(entity, "terrain stale async result", terrain.getRebuildNotBeforeFrame());
            return;
        }

        auto heights = extractTerrainHeights(texture);
        if (heights.empty()) {
            YA_CORE_WARN("Terrain height map has unsupported payload: {}", terrain._heightMapRef.getPath());
            state.state = TerrainRuntimeState::EResolveState::Failed;
            state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
            _activeTerrain.erase(entity);
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
        resource->lastUsedFrame  = currentFrame;
        _terrainDerivedResources[derivedKey] = resource;

        state.currentDerivedKey  = derivedKey;
        state.boundResource      = resource;
        state.pendingHeightMapHandle   = 0;
        state.lastBuiltHeightMapVersion = heightMapVersion;
        state.state                    = TerrainRuntimeState::EResolveState::Ready;
        state.lastCompletedAuthoringVersion = terrain.getAuthoringVersion();
        _activeTerrain.erase(entity);
    };

    while (!_dirtyTerrainQueue.empty()) {
        const auto entity = _dirtyTerrainQueue.front();
        _dirtyTerrainQueue.pop_front();
        _dirtyTerrainSet.erase(entity);
        pumpOne(entity);
    }
}

} // namespace ya
