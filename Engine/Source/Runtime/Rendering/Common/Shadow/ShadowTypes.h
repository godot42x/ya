#pragma once

#include "Render/RenderDefines.h"

#include "Shadow.PointShadowCull.comp.slang.h"

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
constexpr uint32_t CULL_WORKGROUP_SIZE      = slang_types::Shadow::PointShadowCull_comp::POINT_SHADOW_CULL_WORKGROUP_SIZE;
constexpr uint32_t MAX_DRAWS_PER_FACE       = slang_types::Shadow::PointShadowCull_comp::POINT_SHADOW_MAX_DRAWS_PER_FACE;
} // namespace ShadowConstants

// ═══════════════════════════════════════════════════════════════════════════
// GPU-facing structures: single truth comes from generated Slang headers.
// ═══════════════════════════════════════════════════════════════════════════

using PointShadowInstanceData     = slang_types::Shadow::PointShadowCull_comp::ShadowInstanceData;
using PointShadowFaceFrustum      = slang_types::Shadow::PointShadowCull_comp::ShadowFaceFrustum;
using PointShadowIndirectCommand  = slang_types::Shadow::PointShadowCull_comp::IndexedIndirectCommand;
using PointShadowCullPushConstant = slang_types::Shadow::PointShadowCull_comp::PushConstants;

// ═══════════════════════════════════════════════════════════════════════════
// Point Shadow Indirect — Bucket addressing
// ───────────────────────────────────────────────────────────────────────────
// All GPU buffers (cmd / visibleInstances) are addressed by a single key:
//
//     bucket = batch * faceCount + face
//
//   • batch     — index into PointShadowIndirectRenderer::_meshBatches
//                 (= which mesh group this draw belongs to)
//   • face      — global face index = lightIndex * 6 + cubeFace
//   • faceCount = pointLightCount * 6   (active faces this frame)
//
// Layout per (batch, face) bucket:
//
//   uDrawCommands     [bucket]                                  // 1 cmd
//   uVisibleInstances [bucket * MAX_DRAWS_PER_FACE ..  +N]      // up to 4096 ids
//
// The vertex shader receives `bucketBase = bucket * MAX_DRAWS_PER_FACE`
// via push constant and uses SV_InstanceID to fetch its global instance id.
// ═══════════════════════════════════════════════════════════════════════════

namespace PointShadowAddressing
{
constexpr uint32_t bucketIndex(uint32_t batch, uint32_t face, uint32_t faceCount)
{
    return batch * faceCount + face;
}
constexpr uint32_t bucketBaseSlot(uint32_t bucket)
{
    return bucket * ShadowConstants::MAX_DRAWS_PER_FACE;
}
} // namespace PointShadowAddressing

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
        normalizePlane(row2),        // near (RH_ZO clip space: z >= 0)
        normalizePlane(row3 - row2), // far
    };
}

} // namespace ya
