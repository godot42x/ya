#pragma once

#include "Render/RenderDefines.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// Shadow system constants
// ═══════════════════════════════════════════════════════════════════════════

namespace ShadowConstants
{
constexpr uint32_t FACES_PER_POINT_LIGHT    = 6;
constexpr uint32_t POINT_SHADOW_FACE_COUNT  = MAX_POINT_LIGHTS * FACES_PER_POINT_LIGHT; // 36
constexpr uint32_t DIRECTIONAL_SHADOW_COUNT = 1;
constexpr uint32_t TOTAL_SHADOW_LAYERS      = DIRECTIONAL_SHADOW_COUNT + POINT_SHADOW_FACE_COUNT; // 37
constexpr uint32_t CULL_WORKGROUP_SIZE      = 256;
constexpr uint32_t MAX_DRAWS_PER_FACE       = 4096;
} // namespace ShadowConstants

// ═══════════════════════════════════════════════════════════════════════════
// GPU-mirrored structures (must match PointShadowCommon.slang layout)
// ═══════════════════════════════════════════════════════════════════════════

/// Per-instance data uploaded to the GPU instance buffer.
struct alignas(16) PointShadowInstanceData
{
    glm::mat4 worldMatrix{1.0f};
    glm::vec4 boundingSphere{0.0f}; // xyz=center, w=radius (world space)
    uint32_t  indexCount  = 0;
    uint32_t  firstIndex  = 0;
    int32_t   vertexOffset = 0;
    uint32_t  _pad0       = 0;
};

/// Per-face frustum data for the compute cull shader.
struct alignas(16) PointShadowFaceFrustum
{
    glm::vec4 planes[6]; // xyz=normal, w=d (normalized)
};

/// VkDrawIndexedIndirectCommand-compatible structure.
struct PointShadowIndirectCommand
{
    uint32_t indexCount    = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex    = 0;
    int32_t  vertexOffset  = 0;
    uint32_t firstInstance = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// CPU-side helpers
// ═══════════════════════════════════════════════════════════════════════════

/// Frustum plane extraction from a view-projection matrix.
inline std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& viewProj)
{
    auto normalizePlane = [](glm::vec4 plane) -> glm::vec4
    {
        const float len = glm::length(glm::vec3(plane));
        return (len > 1e-6f) ? plane / len : plane;
    };

    const glm::vec4 row0{viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]};
    const glm::vec4 row1{viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]};
    const glm::vec4 row2{viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]};
    const glm::vec4 row3{viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]};

    return {
        normalizePlane(row3 + row0), // left
        normalizePlane(row3 - row0), // right
        normalizePlane(row3 + row1), // bottom
        normalizePlane(row3 - row1), // top
        normalizePlane(row3 + row2), // near
        normalizePlane(row3 - row2), // far
    };
}

} // namespace ya
