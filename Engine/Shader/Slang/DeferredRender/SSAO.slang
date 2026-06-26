#include "Common/Helper.slang"

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float2 uv          : UV;
};

struct FrameData
{
    int2     screenResolution;
    float    radius;
    float    bias;
    float    power;
    float    intensity;
    uint     reverseY;
    float4x4 projectMat;
    float4x4 invProjectMat;
    float4x4 viewMat;
};

[[vk::binding(0,0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

[[vk::binding(0,1)]] Sampler2D uTexRT0;   // RT0: (worldPos.xyz, roughness/shininess)
[[vk::binding(1,1)]] Sampler2D uTexRT1;   // RT1: (worldNormal.xyz, ao/reflectRatio)
[[vk::binding(2,1)]] Sampler2D uTexDepth; // GBuffer depth
[[vk::binding(3,1)]] Sampler2D uTexNoise; // 4x4 tangent rotation noise

static const int SSAO_KERNEL_SIZE = 32;

float rand(float n)
{
    return fract(sin(n) * 43758.5453123);
}

vec3 hemisphereSample(int index)
{
    float fi       = float(index);
    float random0  = rand(fi * 12.9898 + 78.233);
    float random1  = rand(fi * 39.3467 + 11.135);
    float phi      = random0 * 2.0 * PI;
    float cosTheta = random1;
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    vec3 sampleDir = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float scale    = fi / float(SSAO_KERNEL_SIZE);
    scale          = lerp(0.1, 1.0, scale * scale);
    return sampleDir * scale;
}

float2 getClipUV(float2 uv)
{
    float2 clipUV = uv;
    if (uFrame.reverseY != 0) {
        clipUV.y = 1.0 - clipUV.y;
    }
    return clipUV;
}

vec3 reconstructViewPos(float2 uv, float depth)
{
    float2 clipUV = getClipUV(uv);
    float2 ndcXY  = clipUV * 2.0 - 1.0;
    float4 clip   = float4(ndcXY, depth, 1.0);
    float4 view   = mul(uFrame.invProjectMat, clip);
    return view.xyz / max(view.w, 1e-6);
}

[shader("vertex")]
void vertMain(uint vertexId : SV_VertexID, out VertexOutput OUT)
{
    vec2 pos;
    if (vertexId == 0) {
        pos = vec2(-1.0, -1.0);
    }
    else if (vertexId == 1) {
        pos = vec2(3.0, -1.0);
    }
    else {
        pos = vec2(-1.0, 3.0);
    }

    OUT.sv_position = vec4(pos, 0.0, 1.0);
    OUT.uv          = pos * 0.5 + 0.5;
}

[shader("fragment")]
void fragMain(in VertexOutput IN, out vec4 fragColor : SV_Target)
{
    vec3 worldNormal = normalize(uTexRT1.Sample(IN.uv).xyz);
    float depth      = uTexDepth.Sample(IN.uv).r;

    if (length(worldNormal) < 0.0001 || depth >= 0.999999) {
        fragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 viewPos    = reconstructViewPos(IN.uv, depth);
    vec3 viewNormal = normalize(mul((float3x3)uFrame.viewMat, worldNormal));

    float2 noiseScale = float2(float(uFrame.screenResolution.x), float(uFrame.screenResolution.y)) / 4.0;
    vec3 randomVec    = normalize(uTexNoise.Sample(IN.uv * noiseScale).xyz * 2.0 - 1.0);

    vec3 tangent   = normalize(randomVec - viewNormal * dot(randomVec, viewNormal));
    vec3 bitangent = cross(viewNormal, tangent);
    float3x3 tbn   = float3x3(tangent, bitangent, viewNormal);

    float occlusion = 0.0;
    for (int i = 0; i < SSAO_KERNEL_SIZE; ++i) {
        vec3 sampleOffsetVS = mul(tbn, hemisphereSample(i)) * uFrame.radius;
        vec3 samplePosVS    = viewPos + sampleOffsetVS;

        float4 clipPos = mul(uFrame.projectMat, vec4(samplePosVS, 1.0));
        if (abs(clipPos.w) < 1e-6) {
            continue;
        }

        vec3 ndc      = clipPos.xyz / clipPos.w;
        vec2 sampleUV = ndc.xy * 0.5 + 0.5;
        if (uFrame.reverseY != 0) {
            sampleUV.y = 1.0 - sampleUV.y;
        }
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }

        float sampleDepth = uTexDepth.Sample(sampleUV).r;
        if (sampleDepth >= 0.999999) {
            continue;
        }

        vec3 actualSampleVS = reconstructViewPos(sampleUV, sampleDepth);

        float depthDelta = abs(viewPos.z - actualSampleVS.z);
        float rangeCheck = smoothstep(0.0, 1.0, uFrame.radius / max(depthDelta, 1e-4));
        float blocked    = actualSampleVS.z >= samplePosVS.z + uFrame.bias ? 1.0 : 0.0;
        occlusion       += blocked * rangeCheck;
    }

    float ao = 1.0 - occlusion / float(SSAO_KERNEL_SIZE);
    ao       = pow(saturate(ao), max(uFrame.power, 0.001));
    ao       = saturate(1.0 - (1.0 - ao) * max(uFrame.intensity, 0.0));
    fragColor = vec4(ao, ao, ao, 1.0);
}
