#include "Render/Terrain/TerrainMeshBuilder.h"

#include <algorithm>
#include <cmath>

namespace ya
{

namespace
{

constexpr uint32_t TERRAIN_MIN_GRID_RESOLUTION = 2;
constexpr uint32_t TERRAIN_MAX_GRID_RESOLUTION = 1024;

float sampleHeightNearest(const TerrainMeshBuildDesc& desc, uint32_t x, uint32_t y)
{
    if (desc.heights.empty() || desc.heightWidth == 0 || desc.heightHeight == 0) {
        return 0.0f;
    }

    const uint32_t resolution = clampTerrainGridResolution(desc.gridResolution);
    const uint32_t srcX = resolution <= 1 ? 0 : static_cast<uint32_t>(std::round(
        (static_cast<float>(x) / static_cast<float>(resolution - 1)) * static_cast<float>(desc.heightWidth - 1)));
    const uint32_t srcY = resolution <= 1 ? 0 : static_cast<uint32_t>(std::round(
        (static_cast<float>(y) / static_cast<float>(resolution - 1)) * static_cast<float>(desc.heightHeight - 1)));
    const uint32_t clampedX = std::min(srcX, desc.heightWidth - 1);
    const uint32_t clampedY = std::min(srcY, desc.heightHeight - 1);
    const size_t   index    = static_cast<size_t>(clampedY) * desc.heightWidth + clampedX;
    if (index >= desc.heights.size()) {
        return 0.0f;
    }
    return std::clamp(desc.heights[index], 0.0f, 1.0f);
}

void accumulateTriangleNormal(std::vector<Vertex>& vertices, uint32_t i0, uint32_t i1, uint32_t i2)
{
    const glm::vec3& p0 = vertices[i0].position;
    const glm::vec3& p1 = vertices[i1].position;
    const glm::vec3& p2 = vertices[i2].position;

    glm::vec3 normal = glm::cross(p1 - p0, p2 - p0);
    if (glm::dot(normal, normal) > 0.0f) {
        normal = glm::normalize(normal);
    }

    vertices[i0].normal += normal;
    vertices[i1].normal += normal;
    vertices[i2].normal += normal;
}

} // namespace

uint32_t clampTerrainGridResolution(uint32_t value)
{
    return std::clamp(value, TERRAIN_MIN_GRID_RESOLUTION, TERRAIN_MAX_GRID_RESOLUTION);
}

EngineMeshData buildTerrainMeshData(const TerrainMeshBuildDesc& desc)
{
    const uint32_t resolution = clampTerrainGridResolution(desc.gridResolution);
    const float    width      = std::max(desc.size.x, 0.001f);
    const float    depth      = std::max(desc.size.y, 0.001f);
    const float    stepX      = width / static_cast<float>(resolution - 1);
    const float    stepZ      = depth / static_cast<float>(resolution - 1);

    std::vector<Vertex> vertices;
    vertices.resize(static_cast<size_t>(resolution) * resolution);

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t x = 0; x < resolution; ++x) {
            const size_t index = static_cast<size_t>(z) * resolution + x;
            const float  u     = static_cast<float>(x) / static_cast<float>(resolution - 1);
            const float  v     = static_cast<float>(z) / static_cast<float>(resolution - 1);
            const float  h     = sampleHeightNearest(desc, x, z);

            auto& vertex      = vertices[index];
            vertex.position   = glm::vec3(-width * 0.5f + stepX * static_cast<float>(x),
                                           h * desc.heightScale + desc.heightOffset,
                                           -depth * 0.5f + stepZ * static_cast<float>(z));
            vertex.texCoord0  = glm::vec2(u, v);
            vertex.normal     = glm::vec3(0.0f);
            vertex.tangent    = glm::vec3(1.0f, 0.0f, 0.0f);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(resolution - 1) * (resolution - 1) * 6);
    for (uint32_t z = 0; z + 1 < resolution; ++z) {
        for (uint32_t x = 0; x + 1 < resolution; ++x) {
            const uint32_t i0 = z * resolution + x;
            const uint32_t i1 = z * resolution + (x + 1);
            const uint32_t i2 = (z + 1) * resolution + x;
            const uint32_t i3 = (z + 1) * resolution + (x + 1);

            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);

            accumulateTriangleNormal(vertices, i0, i2, i1);
            accumulateTriangleNormal(vertices, i1, i2, i3);
        }
    }

    for (auto& vertex : vertices) {
        if (glm::dot(vertex.normal, vertex.normal) <= 0.0f) {
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        else {
            vertex.normal = glm::normalize(vertex.normal);
        }
    }

    return EngineMeshData{
        .name     = desc.name.empty() ? std::string("terrain") : desc.name,
        .vertices = std::move(vertices),
        .indices  = std::move(indices),
    };
}

} // namespace ya
