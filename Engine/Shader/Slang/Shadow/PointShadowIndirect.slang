#include "Common/Limits.slang"
#include "Shadow/PointShadowCommon.slang"

// ═══════════════════════════════════════════════════════════════════════════
// Point Shadow Indirect Draw Shader
//
// Vertex shader fetches the instance transform via SV_InstanceID which is
// set to the global instance index by the compute cull shader's firstInstance
// field in the indirect command.
//
// Fragment shader outputs linear depth (distance / farPlane) for point-light
// omnidirectional shadow mapping.
// ═══════════════════════════════════════════════════════════════════════════

struct PointLightFaceData
{
    float4x4 viewProj;
    float3   lightPos;
    float    farPlane;
};

[[vk::binding(0, 0)]] ConstantBuffer<PointLightFaceData, Std140DataLayout> uFace;
[[vk::binding(0, 1)]] StructuredBuffer<ShadowInstanceData> uInstances;

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
VertexOutput vertMain(VertexInput input, uint instanceID : SV_InstanceID)
{
    ShadowInstanceData inst = uInstances[instanceID];
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
