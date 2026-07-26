#include "ECS/Component/Terrain/TerrainComponent.h"

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

void TerrainComponent::invalidate()
{
    ++_authoringVersion;
    _heightMapRef.invalidate();
    _runtimeMesh.reset();
    _pendingHeightMapHandle = 0;
    _resolveState = hasHeightMap() ? ETerrainResolveState::Dirty : ETerrainResolveState::Empty;
}

void TerrainComponent::markLoading(AssetManager::TextureBatchMemoryHandle handle)
{
    _pendingHeightMapHandle = handle;
    _resolveState           = ETerrainResolveState::LoadingHeightMap;
}

void TerrainComponent::markFailed()
{
    _runtimeMesh.reset();
    _pendingHeightMapHandle = 0;
    _resolveState           = ETerrainResolveState::Failed;
}

void TerrainComponent::setRuntimeMesh(stdptr<Mesh> mesh, uint64_t heightMapVersion)
{
    _runtimeMesh               = std::move(mesh);
    _lastBuiltHeightMapVersion = heightMapVersion;
    _pendingHeightMapHandle    = 0;
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

} // namespace ya
