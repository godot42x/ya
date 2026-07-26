#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "Render/Mesh.h"
#include "Resource/AssetManager.h"

#include <glm/glm.hpp>

namespace ya
{

enum class ETerrainResolveState : uint8_t
{
    Empty = 0,
    Dirty,
    LoadingHeightMap,
    Ready,
    Failed,
};

struct TerrainComponent : public IComponent
{
    YA_REFLECT_BEGIN(TerrainComponent, IComponent)
    YA_REFLECT_FIELD(_heightMapRef)
    YA_REFLECT_FIELD(_size)
    YA_REFLECT_FIELD(_heightScale)
    YA_REFLECT_FIELD(_heightOffset, .manipulate(-1024.0f, 1024.0f))
    YA_REFLECT_FIELD(_gridResolution, .manipulate(2, 1024))
    YA_REFLECT_END()

    TextureRef _heightMapRef;
    glm::vec2  _size           = glm::vec2(100.0f, 100.0f);
    float      _heightScale    = 20.0f;
    float      _heightOffset   = 0.0f;
    uint32_t   _gridResolution = 128;

    ETerrainResolveState _resolveState = ETerrainResolveState::Empty;

    TerrainComponent();

    [[nodiscard]] bool hasHeightMap() const { return _heightMapRef.hasPath(); }
    [[nodiscard]] bool isResolved() const { return _runtimeMesh != nullptr; }
    [[nodiscard]] bool needsResolve() const;
    [[nodiscard]] Mesh* getMesh() const { return _runtimeMesh.get(); }
    [[nodiscard]] uint64_t getLastBuiltHeightMapVersion() const { return _lastBuiltHeightMapVersion; }
    [[nodiscard]] uint64_t getAuthoringVersion() const { return _authoringVersion; }
    [[nodiscard]] uint64_t getRebuildNotBeforeFrame() const { return _rebuildNotBeforeFrame; }

    void invalidate(uint64_t rebuildNotBeforeFrame = 0);
    void markLoading(AssetManager::TextureBatchMemoryHandle handle);
    void markFailed();
    void setRuntimeMesh(stdptr<Mesh> mesh, uint64_t heightMapVersion);
    void onPostSerialize() override;

    [[nodiscard]] AssetManager::TextureBatchMemoryHandle getPendingHeightMapHandle() const { return _pendingHeightMapHandle; }
    void clearPendingHeightMapHandle() { _pendingHeightMapHandle = 0; }

  private:
    void setupCallbacks();
    void retireRuntimeMesh();

    stdptr<Mesh>                            _runtimeMesh;
    AssetManager::TextureBatchMemoryHandle _pendingHeightMapHandle     = 0;
    uint64_t                               _lastBuiltHeightMapVersion  = 0;
    uint64_t                               _authoringVersion           = 1;
    uint64_t                               _rebuildNotBeforeFrame      = 0;
};

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::ETerrainResolveState)
YA_REFLECT_ENUM_VALUE(Empty)
YA_REFLECT_ENUM_VALUE(Dirty)
YA_REFLECT_ENUM_VALUE(LoadingHeightMap)
YA_REFLECT_ENUM_VALUE(Ready)
YA_REFLECT_ENUM_VALUE(Failed)
YA_REFLECT_ENUM_END()
