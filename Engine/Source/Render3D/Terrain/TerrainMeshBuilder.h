#pragma once

#include "Resource/EngineMeshData.h"
#include "Core/Api.h"

#include <span>
#include <string>

#include <glm/glm.hpp>

namespace ya
{

struct TerrainMeshBuildDesc
{
    std::string      name = "terrain";
    glm::vec2        size = glm::vec2(100.0f, 100.0f);
    float            heightScale = 20.0f;
    float            heightOffset = 0.0f;
    uint32_t         gridResolution = 128;
    uint32_t         heightWidth = 0;
    uint32_t         heightHeight = 0;
    std::span<const float> heights;
};

[[nodiscard]] ENGINE_API uint32_t clampTerrainGridResolution(uint32_t value);
[[nodiscard]] ENGINE_API EngineMeshData buildTerrainMeshData(const TerrainMeshBuildDesc& desc);

} // namespace ya
