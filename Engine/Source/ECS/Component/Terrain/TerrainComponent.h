#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include <glm/glm.hpp>

namespace ya
{

struct ENGINE_API TerrainComponent : public IComponent
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

    TerrainComponent();

    [[nodiscard]] bool hasHeightMap() const { return _heightMapRef.hasPath(); }
    [[nodiscard]] uint64_t getAuthoringVersion() const { return _authoringVersion; }
    [[nodiscard]] uint64_t getRebuildNotBeforeFrame() const { return _rebuildNotBeforeFrame; }

    void invalidate(uint64_t rebuildNotBeforeFrame = 0);
    void setRebuildNotBeforeFrame(uint64_t rebuildNotBeforeFrame) { _rebuildNotBeforeFrame = rebuildNotBeforeFrame; }
    void onPostSerialize() override;

    // Terrain acts as a mesh source for the frame extractor; always considered resolved.
    [[nodiscard]] bool isResolved() const { return true; }
    // Placeholder for draw-item emission; real terrain meshes are produced by ResourceResolveSystem.
    [[nodiscard]] void* getMesh() const { return nullptr; }

  private:
    void setupCallbacks();

    uint64_t _authoringVersion      = 1;
    uint64_t _rebuildNotBeforeFrame = 0;
};

} // namespace ya
