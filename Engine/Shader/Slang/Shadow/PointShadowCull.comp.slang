#include "Common/Limits.slang"
#include "Shadow/PointShadowCommon.slang"

// ═══════════════════════════════════════════════════════════════════════════
// Point Shadow Frustum Cull — Parallel Compute Shader (NoBaseInstance design)
//
// Dispatch: (ceil(instanceCount / WORKGROUP_SIZE), faceCount, 1)
//
// For each visible (instance, face), append the global instance index to
//   visibleInstances[bucketBase + slot]
// where bucket = (batchIndex * faceCount + faceIndex), bucketBase = bucket * MAX_DRAWS_PER_FACE.
//
// Each (batch, face) bucket has ONE pre-initialized indirect cmd whose
// instanceCount we atomically increment. The cmd's other fields
// (indexCount/firstIndex/vertexOffset/firstInstance) are written once by the
// CPU during prepare and never touched by the shader.
//
// Buffer layouts:
//   uDrawCommands[bucketCount]                   (1 cmd / bucket)
//   uVisibleInstances[bucketCount * MAX_DRAWS_PER_FACE]
// ═══════════════════════════════════════════════════════════════════════════

#define POINT_SHADOW_CULL_WORKGROUP_SIZE 256
#define POINT_SHADOW_MAX_DRAWS_PER_FACE 4096

static const uint WORKGROUP_SIZE     = POINT_SHADOW_CULL_WORKGROUP_SIZE;
static const uint MAX_DRAWS_PER_FACE = POINT_SHADOW_MAX_DRAWS_PER_FACE;

[[vk::binding(0, 0)]] StructuredBuffer<ShadowInstanceData>       uInstances;
[[vk::binding(1, 0)]] StructuredBuffer<ShadowFaceFrustum>        uFaceFrustums;
[[vk::binding(2, 0)]] RWStructuredBuffer<IndexedIndirectCommand> uDrawCommands;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>                   uVisibleInstances;

struct PushConstants
{
    uint instanceCount;
    uint faceCount;
    uint batchCount;
    uint _pad;
};

[[vk::push_constant]] ConstantBuffer<PushConstants, Std140DataLayout> pc;

bool isSphereVisible(float4 sphere, ShadowFaceFrustum frustum)
{
    float3 center = sphere.xyz;
    float  radius = sphere.w;
    for (int i = 0; i < 6; ++i) {
        // plane.xyz = inward normal, plane.w = distance from origin
        // dot(n, p) + d >= 0  → on the inside half-space
        float dist = dot(frustum.planes[i].xyz, center) + frustum.planes[i].w;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void compMain(uint3 groupID : SV_GroupID, uint3 localID : SV_GroupThreadID)
{
    uint instanceIndex = groupID.x * WORKGROUP_SIZE + localID.x;
    uint faceIndex     = groupID.y;

    if (instanceIndex >= pc.instanceCount || faceIndex >= pc.faceCount) {
        return;
    }

    ShadowInstanceData inst    = uInstances[instanceIndex];
    ShadowFaceFrustum  frustum = uFaceFrustums[faceIndex];

    if (!isSphereVisible(inst.boundingSphere, frustum)) {
        return;
    }

    uint batchIndex = inst.batchIndex;
    if (batchIndex >= pc.batchCount) {
        return; // safety
    }

    uint bucketIndex = batchIndex * pc.faceCount + faceIndex;

    // Atomic append — also yields this thread's slot inside the bucket.
    uint slot;
    InterlockedAdd(uDrawCommands[bucketIndex].instanceCount, 1, slot);

    if (slot >= MAX_DRAWS_PER_FACE) {
        // Bucket overflow — instanceCount has been over-incremented; clamp
        // it back so the GPU doesn't try to read past the lookup buffer.
        InterlockedMin(uDrawCommands[bucketIndex].instanceCount, MAX_DRAWS_PER_FACE);
        return;
    }

    uVisibleInstances[bucketIndex * MAX_DRAWS_PER_FACE + slot] = instanceIndex;
}
