struct VertexOutput
{
    float4 clipPos : SV_Position;
    float2 uv : TEXCOORD_UV;
};

struct PushConstants
{
    float2 texelSize;
    uint horizontal;
    float pad;
};

[[vk::push_constant]] PushConstants pc;

Texture2D<float4> inputTexture : register(t0, space0);
SamplerState inputSampler : register(s0, space0);

static const float weights[5] = {
    0.2270270270,
    0.1945945946,
    0.1216216216,
    0.0540540541,
    0.0162162162,
};

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
    float2 axis = pc.horizontal != 0u ? float2(pc.texelSize.x, 0.0) : float2(0.0, pc.texelSize.y);
    float3 result = inputTexture.Sample(inputSampler, input.uv).rgb * weights[0];

    [unroll]
    for (int i = 1; i < 5; ++i) {
        float2 offset = axis * float(i);
        result += inputTexture.Sample(inputSampler, input.uv + offset).rgb * weights[i];
        result += inputTexture.Sample(inputSampler, input.uv - offset).rgb * weights[i];
    }

    return float4(result, 1.0);
}
