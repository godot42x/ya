#include "Common/Helper.slang"

// Screen-space reflection (SSR): a small, self-contained proof of the
// extension-slot protocol. It reads the lit scene colour, the GBuffer world
// normal and the depth, marches a reflection ray in view space against the
// depth buffer, and blends the hit colour back over the scene.
//
// This is deliberately a simple SSR (fixed step count, no temporal reuse, no
// roughness cone): its job is to be a *real* new effect that exercises the
// AfterLighting extension point, not to be production-quality. A real SSR would
// replace the loop below and keep the same feature wiring.

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float2 uv          : UV;
};

struct FrameData
{
    int2     screenResolution;
    float    maxDistance;
    float    thickness;
    float    intensity;
    uint     stepCount;
    uint     reverseY;
    float    fadeEdge;
    float4x4 projectMat;
    float4x4 invProjectMat;
    float4x4 viewMat;
};

[[vk::binding(0,0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

[[vk::binding(0,1)]] Sampler2D uTexSceneColor; // lit scene colour
[[vk::binding(1,1)]] Sampler2D uTexNormal;     // RT1: (worldNormal.xyz, ao)
[[vk::binding(2,1)]] Sampler2D uTexDepth;      // GBuffer depth

float2 getClipUV(float2 uv)
{
    float2 clipUV = uv;
    if (uFrame.reverseY != 0) {
        clipUV.y = 1.0 - clipUV.y;
    }
    return clipUV;
}

float2 viewPosToUV(float3 viewPos)
{
    float4 clip = mul(uFrame.projectMat, float4(viewPos, 1.0));
    if (abs(clip.w) < 1e-6) {
        return float2(-1.0, -1.0);
    }
    float2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
    if (uFrame.reverseY != 0) {
        uv.y = 1.0 - uv.y;
    }
    return uv;
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

    OUT.sv_position = float4(pos, 0.0, 1.0);
    OUT.uv          = pos * 0.5 + 0.5;
}

[shader("fragment")]
void fragMain(in VertexOutput IN, out vec4 fragColor : SV_Target)
{
    float3 sceneColor = uTexSceneColor.Sample(IN.uv).rgb;

    float depth = uTexDepth.Sample(IN.uv).r;
    // No geometry here (background): nothing to reflect from.
    if (depth >= 0.999999) {
        fragColor = float4(sceneColor, 1.0);
        return;
    }

    float3 worldNormal = normalize(uTexNormal.Sample(IN.uv).xyz);
    if (length(worldNormal) < 0.0001) {
        fragColor = float4(sceneColor, 1.0);
        return;
    }

    vec3 viewPos    = reconstructViewPos(IN.uv, depth);
    vec3 viewNormal = normalize(mul((float3x3)uFrame.viewMat, worldNormal));

    // View-space eye ray and its mirror about the surface normal.
    vec3 viewDir    = normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, viewNormal));

    // A reflection heading into the surface cannot hit anything in screen
    // space: leave the pixel alone.
    if (reflectDir.z < 0.0) {
        fragColor = float4(sceneColor, 1.0);
        return;
    }

    float stepLen = uFrame.maxDistance / max(float(uFrame.stepCount), 1.0);
    float2 hitUV  = float2(-1.0, -1.0);
    float  hitFade = 0.0;

    for (uint i = 1u; i <= uFrame.stepCount; ++i) {
        float3 samplePos = viewPos + reflectDir * (stepLen * float(i));

        float2 uv = viewPosToUV(samplePos);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            break;
        }

        float sceneDepth = uTexDepth.Sample(uv).r;
        if (sceneDepth >= 0.999999) {
            continue;
        }

        vec3 scenePos = reconstructViewPos(uv, sceneDepth);
        // The ray crossed behind existing geometry: this is the hit.
        if (scenePos.z <= samplePos.z && (samplePos.z - scenePos.z) <= uFrame.thickness) {
            hitUV   = uv;
            hitFade = 1.0 - float(i) / float(uFrame.stepCount);
            break;
        }
    }

    float3 result = sceneColor;
    if (hitUV.x >= 0.0) {
        float3 reflection = uTexSceneColor.Sample(hitUV).rgb;

        // Fade the reflection at the screen border so a hit near the edge does
        // not pop.
        float2 edge     = min(hitUV, 1.0 - hitUV);
        float  edgeFade = saturate(min(edge.x, edge.y) / max(uFrame.fadeEdge, 1e-4));

        // Fresnel: reflections are strongest at grazing angles.
        float3 viewDirW = normalize(-viewPos);
        float  fresnel  = pow(1.0 - saturate(dot(viewNormal, viewDirW)), 5.0);
        fresnel         = lerp(0.05, 1.0, fresnel);

        float weight = saturate(uFrame.intensity * fresnel * hitFade * edgeFade);
        result       = lerp(sceneColor, reflection, weight);
    }

    fragColor = float4(result, 1.0);
}
