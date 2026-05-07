#include "Common/Limits.slang"
#include "Shadow/PointShadowCommon.slang"

// ═══════════════════════════════════════════════════════════════════════════
// Point Shadow Frustum Cull — Parallel Compute Shader
//
// Dispatch: (ceil(instanceCount / WORKGROUP_SIZE), activeFaceCount, 1)
// Each thread tests one (instance, face) pair. Visible instances are
// compacted via atomics into a per-face indirect draw command stream.
// ═══════════════════════════════════════════════════════════════════════════

static const uint WORKGROUP_SIZE = 256;
static const uint MAX_DRAWS_PER_FACE = 4096;

[[vk::binding(0, 0)]] StructuredBuffer<ShadowInstanceData>       uInstances;
[[vk::binding(1, 0)]] StructuredBuffer<ShadowFaceFrustum>        uFaceFrustums;
[[vk::binding(2, 0)]] RWStructuredBuffer<IndexedIndirectCommand> uDrawCommands;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>                   uDrawCounts;

struct PushConstants
{
    uint instanceCount;
    uint faceCount;
};

[[vk::push_constant]] ConstantBuffer <PushConstants,Std140DataLayout> pc;

bool isSphereVisible(float4 sphere, ShadowFaceFrustum frustum)
{
    float3 center = sphere.xyz;
    float  radius = sphere.w;
    for (int i = 0; i < 6; ++i) {
        // plane.xyz = normal, plane.w = distance
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

    // Atomic append: get slot in this face's draw command region
    uint slot;
    InterlockedAdd(uDrawCounts[faceIndex], 1, slot);

    if (slot >= MAX_DRAWS_PER_FACE) {
        return; // overflow protection
    }

    // Write one indirect draw command per visible instance
    uint cmdIndex = faceIndex * MAX_DRAWS_PER_FACE + slot;
    IndexedIndirectCommand cmd;
    cmd.indexCount    = inst.indexCount;
    cmd.instanceCount = 1;
    cmd.firstIndex    = inst.firstIndex;
    cmd.vertexOffset  = inst.vertexOffset;
    cmd.firstInstance = instanceIndex; // used in VS to fetch instance transform
    uDrawCommands[cmdIndex] = cmd;
}
