#include "Common/Limits.slang"
#include "Shadow/PointShadowCommon.slang"
#include "Shadow/PointShadowFace.slang"

// ═══════════════════════════════════════════════════════════════════════════
// Point Shadow Indirect Draw — BaseInstance design.
//
// Each draw call corresponds to one (batch, face) bucket. The CPU bakes
// `bucketBase = bucketIndex * MAX_DRAWS_PER_FACE` into the indirect command's
// firstInstance field, so the VS can index into `uVisibleInstances` using
// (SV_StartInstanceLocation + SV_InstanceID):
//     globalInstance = uVisibleInstances[bucketBase + localInstanceIndex]
//
// Fragment shader writes linear depth (distance / farPlane) for omni shadow.
// ═══════════════════════════════════════════════════════════════════════════

[[vk::binding(0, 0)]] ConstantBuffer<PointShadowFaceData, Std140DataLayout> uFace;
[[vk::binding(0, 1)]] StructuredBuffer<ShadowInstanceData> uInstances;
[[vk::binding(1, 1)]] StructuredBuffer<uint>               uVisibleInstances;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float3 worldPos    : WORLD_POS;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input,
                      uint startInstance : SV_StartInstanceLocation,
                      uint instanceID     : SV_InstanceID)
{
    uint globalInstance = uVisibleInstances[startInstance + instanceID];
    ShadowInstanceData inst = uInstances[globalInstance];

    float4 worldPos = mul(inst.worldMatrix, float4(input.position, 1.0));

    VertexOutput output;
    output.sv_position = mul(uFace.viewProj, worldPos);
    output.worldPos    = worldPos.xyz;
    return output;
}

[shader("fragment")]
float fragMain(VertexOutput input) : SV_Depth
{
    float dist = length(input.worldPos - uFace.lightPos);
    return dist / uFace.farPlane;
}
