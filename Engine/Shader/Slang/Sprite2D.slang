#include "Common/Limits.slang"

#ifndef TEXTURE_SET_SIZE
#define TEXTURE_SET_SIZE 16
#endif

struct FrameData
{
    float4x4 viewProj;
    float4x4 view;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

[[vk::binding(0, 1)]] Sampler2D uTextures[TEXTURE_SET_SIZE];

struct VertexInput
{
    [[vk::location(0)]] float3 pos         : POSITION;
    [[vk::location(1)]] float4 color       : COLOR0;
    [[vk::location(2)]] float2 texCoord    : TEXCOORD;
    [[vk::location(3)]] uint   textureIdx  : TEXCOORD_TEXTURE_IDX;
    [[vk::location(4)]] float3 worldCenter : TEXCOORD_WORLD_CENTER;
    [[vk::location(5)]] float3 worldDirection : TEXCOORD_WORLD_DIR;
    [[vk::location(6)]] float2 worldSize   : TEXCOORD_WORLD_SIZE;
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float4 color       : COLOR0;
    float2 texCoord    : TEXCOORD;
    nointerpolation uint textureIdx : TEXCOORD_TEXTURE_IDX;
};

float4 sampleTexturedSprite(VertexOutput input)
{
    float4 texColor = uTextures[input.textureIdx].Sample(input.texCoord);
    if (texColor.a < 0.01) { discard; }
    return texColor * input.color;
}

float2 flipWorldUv(float2 uv)
{
    return float2(uv.x, 1.0 - uv.y);
}

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.sv_position = mul(uFrame.viewProj, float4(input.pos, 1.0));
    output.color       = input.color;
    output.texCoord    = input.texCoord;
    output.textureIdx  = input.textureIdx;
    return output;
}

[shader("vertex")]
VertexOutput vertWorldMain(VertexInput input)
{
    float3 cameraRight = normalize(uFrame.view[0].xyz);
    float3 cameraUp    = normalize(uFrame.view[1].xyz);
    float2 centered    = (input.pos.xy - float2(0.5, 0.5)) * input.worldSize;
    float3 worldPos    = input.worldCenter + cameraRight * centered.x + cameraUp * centered.y;

    VertexOutput output;
    output.sv_position = mul(uFrame.viewProj, float4(worldPos, 1.0));
    output.color       = input.color;
    output.texCoord    = flipWorldUv(input.texCoord);
    output.textureIdx  = input.textureIdx;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    return sampleTexturedSprite(input);
}
