#include "ECS/Component/Terrain/TerrainComponent.h"

#include "Resource/DeferredDeletionQueue.h"

namespace ya
{

TerrainComponent::TerrainComponent()
{
    setupCallbacks();
}

bool TerrainComponent::needsResolve() const
{
    if (!hasHeightMap()) {
        return false;
    }

    return _resolveState == ETerrainResolveState::Dirty ||
           _resolveState == ETerrainResolveState::Empty ||
           _resolveState == ETerrainResolveState::LoadingHeightMap;
}

void TerrainComponent::invalidate(uint64_t rebuildNotBeforeFrame)
{
    ++_authoringVersion;
    _heightMapRef.invalidate();
    _rebuildNotBeforeFrame = rebuildNotBeforeFrame;
    if (hasHeightMap()) {
        _resolveState = ETerrainResolveState::Dirty;
        return;
    }

    retireRuntimeMesh();
    _pendingHeightMapHandle = 0;
    _resolveState = ETerrainResolveState::Empty;
}

void TerrainComponent::markLoading(AssetManager::TextureBatchMemoryHandle handle)
{
    _pendingHeightMapHandle = handle;
    _resolveState           = ETerrainResolveState::LoadingHeightMap;
}

void TerrainComponent::markFailed()
{
    retireRuntimeMesh();
    _pendingHeightMapHandle = 0;
    _resolveState           = ETerrainResolveState::Failed;
}

void TerrainComponent::setRuntimeMesh(stdptr<Mesh> mesh, uint64_t heightMapVersion)
{
    retireRuntimeMesh();
    _runtimeMesh               = std::move(mesh);
    _lastBuiltHeightMapVersion = heightMapVersion;
    _pendingHeightMapHandle    = 0;
    _rebuildNotBeforeFrame     = 0;
    _resolveState              = _runtimeMesh ? ETerrainResolveState::Ready : ETerrainResolveState::Failed;
}

void TerrainComponent::onPostSerialize()
{
    setupCallbacks();
    invalidate();
}

void TerrainComponent::setupCallbacks()
{
    _heightMapRef.onModified.addLambda(this, [this]() {
        invalidate();
    });
}

void TerrainComponent::retireRuntimeMesh()
{
    if (!_runtimeMesh) {
        return;
    }
    DeferredDeletionQueue::get().retireResource(std::move(_runtimeMesh));
}

} // namespace ya
