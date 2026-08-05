#include "Common/Limits.slang"

struct FrameData
{
    float4x4 viewProj;
    float4x4 view;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

struct LineVertexInput
{
    [[vk::location(0)]] float3 pos   : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
};

struct LineVertexOutput
{
    float4 sv_position : SV_Position;
    float4 color       : COLOR0;
};

[shader("vertex")]
LineVertexOutput vertLineMain(LineVertexInput input)
{
    LineVertexOutput output;
    output.sv_position = mul(uFrame.viewProj, float4(input.pos, 1.0));
    output.color       = input.color;
    return output;
}

[shader("fragment")]
float4 fragLineMain(LineVertexOutput input) : SV_Target
{
    return input.color;
}
