enum EPostProcessingFlag
{
    Inversion = 1,
    ToneMapping = 2,
    Gamma = 4,
    RandomGrain = 8,
};

enum EGrayscaleMode
{
    None = 0,
    Average = 1,
    Weighted = 2,
};

enum EKernelMode
{
    KernelNone = 0,
    Sharpen = 1,
    Blur = 2,
    EdgeDetection = 3,
};

enum EToneMappingCurve
{
    ACES = 0,
    Uncharted2 = 1,
};

struct PushConstants
{
    uint flags;
    uint grayscaleMode;
    uint kernelMode;
    uint toneMappingCurve;
    float4 params0;
};

[[vk::push_constant]] PushConstants pc;

Texture2D<float4> inputTexture : register(t0, space0);
SamplerState inputSampler : register(s0, space0);

struct VertexOutput
{
    float4 clipPos : SV_Position;
    float2 uv : TEXCOORD0;
};

float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float3 acesApprox(float3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 uncharted2ToneMap(float3 color)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    const float W = 11.2;

    float3 mapped = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
    float  white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
    return saturate(mapped / white);
}

float3 sampleKernel(float2 uv, float texelOffset, uint kernelMode)
{
    static const float sharpenKernel[9] = { -1.0, -1.0, -1.0, -1.0, 9.0, -1.0, -1.0, -1.0, -1.0 };
    static const float blurKernel[9] = { 1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0, 2.0 / 16.0, 4.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0 };
    static const float edgeKernel[9] = { 1.0, 1.0, 1.0, 1.0, -8.0, 1.0, 1.0, 1.0, 1.0 };

    float2 offsets[9] = {
        float2(-texelOffset, texelOffset),
        float2(0.0, texelOffset),
        float2(texelOffset, texelOffset),
        float2(-texelOffset, 0.0),
        float2(0.0, 0.0),
        float2(texelOffset, 0.0),
        float2(-texelOffset, -texelOffset),
        float2(0.0, -texelOffset),
        float2(texelOffset, -texelOffset),
    };

    float3 result = float3(0.0, 0.0, 0.0);
    [unroll]
    for (int i = 0; i < 9; ++i) {
        float weight = 0.0;
        if (kernelMode == EKernelMode::Sharpen) {
            weight = sharpenKernel[i];
        }
        else if (kernelMode == EKernelMode::Blur) {
            weight = blurKernel[i];
        }
        else if (kernelMode == EKernelMode::EdgeDetection) {
            weight = edgeKernel[i];
        }

        result += inputTexture.Sample(inputSampler, uv + offsets[i]).rgb * weight;
    }
    return result;
}

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
    float4 baseSample = inputTexture.Sample(inputSampler, input.uv);
    float3 color = baseSample.rgb;

    if (pc.kernelMode != EKernelMode::KernelNone) {
        color = sampleKernel(input.uv, max(pc.params0.y, 0.000001), pc.kernelMode);
    }

    if ((pc.flags & EPostProcessingFlag::Inversion) != 0u) {
        color = 1.0 - color;
    }

    if (pc.grayscaleMode == EGrayscaleMode::Average) {
        float gray = (color.r + color.g + color.b) / 3.0;
        color = float3(gray, gray, gray);
    }
    else if (pc.grayscaleMode == EGrayscaleMode::Weighted) {
        float gray = dot(color, float3(0.2126, 0.7152, 0.0722));
        color = float3(gray, gray, gray);
    }

    if ((pc.flags & EPostProcessingFlag::RandomGrain) != 0u) {
        float noise = hash21(input.uv * 4096.0) * 2.0 - 1.0;
        color += noise * pc.params0.z;
    }

    if ((pc.flags & EPostProcessingFlag::ToneMapping) != 0u) {
        if (pc.toneMappingCurve == EToneMappingCurve::Uncharted2) {
            color = uncharted2ToneMap(color);
        }
        else {
            color = acesApprox(color);
        }
    }

    if ((pc.flags & EPostProcessingFlag::Gamma) != 0u) {
        color = pow(max(color, float3(0.0, 0.0, 0.0)), 1.0 / max(pc.params0.x, 0.001));
    }

    return float4(color, baseSample.a);
}