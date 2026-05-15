#include "Common/Limits.slang"
#include "Common/Skinning.slang"
#include "Shadow/PointShadowFace.slang"

struct PushConstants
{
    float4x4 modelMat;
    int      skinningPaletteIndex;
};

[[vk::push_constant]] PushConstants pc;

struct PointLightData
{
    float4x4 matrix[6];
    float3   pos;
    float    farPlane;
};

struct FrameData
{
    float4x4      directionalLightMatrix;
    PointLightData pointLights[MAX_POINT_LIGHTS];
    uint           numPointLights;
    uint           hasDirectionalLight;
};

#if POINT_SHADOW_FACE_DATA
[[vk::binding(0, 0)]] ConstantBuffer<PointShadowFaceData, Std140DataLayout> uPointFace;
#else
[[vk::binding(0, 0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;
#endif

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    YA_SKINNING_VERTEX_FIELDS
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float4 worldPos    : FRAG_POS;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    float4 localPos      = float4(input.position, 1.0);
    float3 localNormal   = float3(0.0, 0.0, 1.0);
    float3 localTangent  = float3(1.0, 0.0, 0.0);

#if ENABLE_SKINNING
    SkinningResult skinning = applySkinning(
        input.position,
        localNormal,
        localTangent,
        pc.skinningPaletteIndex,
        input.boneIDs,
        input.weights);
    localPos = skinning.localPos;
#endif

    float4 worldPos = mul(pc.modelMat, localPos);

    VertexOutput output;
    output.worldPos = worldPos;
#if POINT_SHADOW_FACE_DATA
    output.sv_position = mul(uPointFace.viewProj, worldPos);
#else
    if (uFrame.hasDirectionalLight == 1) {
        output.sv_position = mul(uFrame.directionalLightMatrix, worldPos);
    }
    else {
        output.sv_position = mul(uFrame.pointLights[0].matrix[0], worldPos);
    }
#endif
    return output;
}

[shader("fragment")]
float fragMain(VertexOutput input) : SV_Depth
{
#if POINT_SHADOW_FACE_DATA
    float lightDistance = length(input.worldPos.xyz - uPointFace.lightPos);
    return lightDistance / uPointFace.farPlane;
#else
    // Directional shadow: hardware depth (SV_Position.z) is correct
    if (uFrame.hasDirectionalLight == 1) {
        return input.sv_position.z;
    }

    // Point shadow: linear distance normalized by far plane
    float lightDistance = length(input.worldPos.xyz - uFrame.pointLights[0].pos);
    return lightDistance / uFrame.pointLights[0].farPlane;
#endif
}
