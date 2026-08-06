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

[shader("fragment")]
uint fragMain(VertexOutput input) : SV_Target
{
    return pc.entityId;
}
