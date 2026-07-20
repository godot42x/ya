#include "Common/Limits.slang"

struct VertexOutput
{
    float4 clipPos : SV_Position;
    float2 uv : TEXCOORD0;
};

struct PushConstants
{
    float threshold;
    float knee;
    float intensity;
    float pad;
};

[[vk::push_constant]] PushConstants pc;

Texture2D<float4> inputTexture : register(t0, space0);
SamplerState inputSampler : register(s0, space0);

[shader("vertex")]
void vertMain(uint vertexId : SV_VertexID, out VertexOutput output)
{
    float2 pos;
    if (vertexId == 0) {
        pos = float2(-1.0, -1.0);
    }
    else if (vertexId == 1) {
        pos = float2(3.0, -1.0);
    }
    else {
        pos = float2(-1.0, 3.0);
    }

    output.uv = pos * 0.5 + 0.5;
    output.clipPos = float4(pos, 0.0, 1.0);
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float3 color = inputTexture.Sample(inputSampler, input.uv).rgb;
    float brightness = max(color.r, max(color.g, color.b));

    float threshold = max(pc.threshold, 0.0);
    float knee = max(pc.knee, 0.0001);
    float soft = saturate((brightness - threshold + knee) / (2.0 * knee));
    soft = soft * soft * (3.0 - 2.0 * soft);
    float hard = step(threshold, brightness);
    float weight = max(soft, hard);

    float3 bloom = color * weight * max(pc.intensity, 0.0);
    return float4(clamp(bloom, 0.0, BLOOM_FP16_MAX), 1.0);
}
