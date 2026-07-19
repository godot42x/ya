struct VertexOutput
{
    float4 clipPos : SV_Position;
    float2 uv : TEXCOORD0;
};

struct PushConstants
{
    float bloomStrength;
    uint bloomEnabled;
    float pad0;
    float pad1;
};

[[vk::push_constant]] PushConstants pc;

Texture2D<float4> sceneTexture : register(t0, space0);
SamplerState sceneSampler : register(s0, space0);

Texture2D<float4> bloomTexture : register(t1, space0);
SamplerState bloomSampler : register(s1, space0);

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
    float4 scene = sceneTexture.Sample(sceneSampler, input.uv);
    float3 color = scene.rgb;
    if (pc.bloomEnabled != 0u && pc.bloomStrength > 0.0) {
        color += bloomTexture.Sample(bloomSampler, input.uv).rgb * pc.bloomStrength;
    }
    return float4(clamp(color, -65504.0, 65504.0), scene.a);
}
