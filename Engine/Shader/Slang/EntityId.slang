#include "Common/Limits.slang"
#include "Common/Skinning.slang"

#ifndef ENABLE_SKINNING
    #define ENABLE_SKINNING 0
#endif
#ifndef SKINNING_SET_INDEX
    #define SKINNING_SET_INDEX 3
#endif

struct FrameData
{
    float4x4 viewProj;
    float4x4 view;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

struct PushConstants
{
    float4x4 modelMat;
    uint     entityId;
    int      skinningPaletteIndex;
    float3   worldCenter;
    float2   worldSize;
};

[[vk::push_constant]] PushConstants pc;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    YA_SKINNING_VERTEX_FIELDS
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    SkinningResult skin = makeDefaultSkinningResult(input.position, float3(0.0), float3(0.0));
#if ENABLE_SKINNING
    skin = applySkinning(input.position, float3(0.0), float3(0.0), pc.skinningPaletteIndex, input.boneIDs, input.weights);
#endif
    output.sv_position = mul(uFrame.viewProj, mul(pc.modelMat, skin.localPos));
    return output;
}

// Camera-facing billboard quad, generated from the vertex id with the same
// right/up math as Misc/BillboardWorld.slang so the id matches the overlay.
[shader("vertex")]
VertexOutput billboardVertMain(uint vertexId : SV_VertexID)
{
    // Triangle list of 6 vertices: (0,1,2) and (3,4,5) => (-1,-1)(1,-1)(-1,1)(-1,1)(1,-1)(1,1)
    const float2 uv = float2(vertexId == 1 || vertexId == 4 || vertexId == 5 ? 1.0 : 0.0,
                             vertexId >= 2 ? 1.0 : 0.0);
    const float2 corner = uv * 2.0 - 1.0;

    const float3 cameraRight = normalize(uFrame.view[0].xyz);
    const float3 cameraUp    = normalize(uFrame.view[1].xyz);
    const float3 worldPos    = pc.worldCenter + cameraRight * (corner.x * 0.5 * pc.worldSize.x)
                                            + cameraUp * (corner.y * 0.5 * pc.worldSize.y);

    VertexOutput output;
    output.sv_position = mul(uFrame.viewProj, float4(worldPos, 1.0));
    return output;
}

[shader("fragment")]
uint fragMain(VertexOutput input) : SV_Target
{
    return pc.entityId;
}
