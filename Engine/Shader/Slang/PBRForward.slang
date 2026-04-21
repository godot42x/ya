#include "Common/Limits.slang"
#include "Common/Helper.slang"

struct VertexInput
{
    [[vk::location(0)]] float3 pos     : POSITION;
    [[vk::location(1)]] float2 uv      : TEXCOORD0;
    [[vk::location(2)]] float3 normal  : NORMAL;
    [[vk::location(3)]] float3 tangent : TANGENT;
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float3 worldPos    : WorldPos;
    float2 uv          : UV;
    float3 normal      : Normal;
    float3 tangent     : Tangent;
    float3 bitangent   : Bitangent;
};

struct PushConstants
{
    float4x4 modelMat;
};

[[vk::push_constant]] PushConstants pc;

struct FrameData
{
    float4x4 projMat;
    float4x4 viewMat;
    float3   cameraPos;
};

struct DirectionalLight
{
    float3   dir;
    float3   color;
    float    intensity;
    float4x4 shadowMatrix;
};

struct PointLight
{
    float3 pos;
    float3 color;
    float  intensity;
    float  farPlane;
};

struct LightData
{
    DirectionalLight dirLight;
    PointLight       pointLights[MAX_POINT_LIGHTS];
    bool             hasDirLight;
    uint             numPointLight;
};

enum ETextureSlot
{
    Albedo    = 0,
    Normal    = 1,
    Metallic  = 2,
    Roughness = 3,
    AO        = 4,
    Count     = 5,
};

struct TextureParams
{
    bool  bEnable;
    float rotationRadius;
    vec2  translation;
    vec2  scale;
};

struct PBRParamsData
{
    vec3  albedo;
    float metallic;
    float roughness;
    float ao;
    TextureParams textures[ETextureSlot::Count];
};

[[vk::binding(0,0)]] ConstantBuffer<FrameData,Std140DataLayout> uFrame;
[[vk::binding(1,0)]] ConstantBuffer<LightData,Std140DataLayout> uLight;

[[vk::binding(0,1)]] Sampler2D uTexAlbedo;
[[vk::binding(1,1)]] Sampler2D uTexNormal;
[[vk::binding(2,1)]] Sampler2D uTexMetallic;
[[vk::binding(3,1)]] Sampler2D uTexRoughness;
[[vk::binding(4,1)]] Sampler2D uTexAO;

[[vk::binding(0,2)]] ConstantBuffer<PBRParamsData,Std140DataLayout> uParams;

[[vk::binding(0,3)]] SamplerCube uTexSkyBoxMap;
[[vk::binding(1,3)]] SamplerCube uTexPBRIrradianceCubemap;
[[vk::binding(2,3)]] SamplerCube uTexPBRPrefilterMap;
[[vk::binding(3,3)]] Sampler2D   uTexPBRBrdfLUT;

[[vk::binding(0,4)]] Sampler2D   uDirectionalLightShadowMap;
[[vk::binding(1,4)]] SamplerCube uPointLightCubeArray[MAX_POINT_LIGHTS];

#ifndef YA_DEFERRED_PBR_ENABLE_IBL_DIFFUSE
    #define YA_DEFERRED_PBR_ENABLE_IBL_DIFFUSE 0
#endif

#ifndef YA_DEFERRED_PBR_ENABLE_IBL_SPECULAR
    #define YA_DEFERRED_PBR_ENABLE_IBL_SPECULAR 0
#endif

#ifndef YA_DEFERRED_ENABLE_SHADOW_MAPPING
    #define YA_DEFERRED_ENABLE_SHADOW_MAPPING 0
#endif

#ifndef YA_DEFERRED_ENABLE_POINT_LIGHT_SHADOW
    #define YA_DEFERRED_ENABLE_POINT_LIGHT_SHADOW 0
#endif

static const float PBR_PREFILTER_MAX_REFLECTION_LOD = 4.0;

vec2 getUV(ETextureSlot slot, vec2 uv)
{
    TextureParams params = uParams.textures[(int)slot];
    uv += params.translation;
    float c = cos(params.rotationRadius);
    float s = sin(params.rotationRadius);
    uv = vec2(
        uv.x * c - uv.y * s,
        uv.x * s + uv.y * c
    );
    uv *= params.scale;
    return uv;
}

vec4 sampleTexture(in Sampler2D tex, ETextureSlot slot, vec2 uv)
{
    if (!uParams.textures[(int)slot].bEnable) {
        return vec4(1.0);
    }
    return tex.Sample(getUV(slot, uv));
}

float calculateDirectionalShadow(float3 worldPos, float3 norm, float3 lightDir)
{
#if !YA_DEFERRED_ENABLE_SHADOW_MAPPING
    return 0.0;
#else
    float4 fragLightSpacePos = mul(uLight.dirLight.shadowMatrix, float4(worldPos, 1.0));
    float w = fragLightSpacePos.w;
    if (abs(w) < 1e-6) {
        return 0.0;
    }

    float3 projCoords = fragLightSpacePos.xyz / w;
    float2 uv = projCoords.xy * 0.5 + 0.5;
    float curDepth = projCoords.z;
    if (curDepth < 0.0 || curDepth > 1.0) {
        float curDepth01 = curDepth * 0.5 + 0.5;
        if (curDepth01 >= 0.0 && curDepth01 <= 1.0) {
            curDepth = curDepth01;
        }
    }

    if (curDepth <= 0.0 || curDepth >= 1.0 || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 0.0;
    }

    float bias = max(0.05 * (1.0 - dot(norm, lightDir)), 0.005);
    float closestDepth = uDirectionalLightShadowMap.Sample(uv).r;
    return (curDepth - bias) > closestDepth ? 1.0 : 0.0;
#endif
}

float calculatePointShadow(uint pointLightIndex, float3 worldPos)
{
#if !YA_DEFERRED_ENABLE_SHADOW_MAPPING || !YA_DEFERRED_ENABLE_POINT_LIGHT_SHADOW
    return 0.0;
#else
    float3 sampleDir = worldPos - uLight.pointLights[pointLightIndex].pos;
    float currentDepth = length(sampleDir);
    if (currentDepth <= 0.0001 || uLight.pointLights[pointLightIndex].farPlane <= 0.0) {
        return 0.0;
    }

    float closestDepth = uPointLightCubeArray[pointLightIndex].Sample(sampleDir).x;
    closestDepth *= uLight.pointLights[pointLightIndex].farPlane;
    float bias = 0.05;
    return (currentDepth - bias) > closestDepth ? 1.0 : 0.0;
#endif
}

float D_normalDistribution_GGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float G_geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return G_geometrySchlickGGX(NdotV, roughness) * G_geometrySchlickGGX(NdotL, roughness);
}

vec3 F_fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 F_fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 shadePBR(in vec3 worldPos, in vec3 N, in vec3 V, in vec3 albedo, float metallic, float roughness, float ao)
{
    vec3 F0 = lerp(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    if (uLight.hasDirLight)
    {
        vec3 L = normalize(-uLight.dirLight.dir);
        vec3 H = normalize(V + L);
        vec3 radiance = uLight.dirLight.color * uLight.dirLight.intensity;
        float shadow = calculateDirectionalShadow(worldPos, N, L);

        float NDF = D_normalDistribution_GGX(N, H, roughness);
        float G = G_geometrySmith(N, V, L, roughness);
        vec3 F = F_fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        vec3 specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);

        Lo += (1.0 - shadow) * (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
    }

    for (uint i = 0; i < uLight.numPointLight; ++i)
    {
        vec3 L = normalize(uLight.pointLights[i].pos - worldPos);
        vec3 H = normalize(V + L);
        float dist = length(uLight.pointLights[i].pos - worldPos);
        vec3 radiance = uLight.pointLights[i].color * uLight.pointLights[i].intensity / max(dist * dist, 0.0001);
        float shadow = calculatePointShadow(i, worldPos);

        float NDF = D_normalDistribution_GGX(N, H, roughness);
        float G = G_geometrySmith(N, V, L, roughness);
        vec3 F = F_fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        vec3 specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.000001);

        Lo += (1.0 - shadow) * (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
    }

    float NdotV = max(dot(N, V), 0.0);
    vec3 kS = F_fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 diffuseIBL = vec3(0.0);
    vec3 specularIBL = vec3(0.0);

#if YA_DEFERRED_PBR_ENABLE_IBL_DIFFUSE
    vec3 irradiance = uTexPBRIrradianceCubemap.Sample(N).rgb;
    diffuseIBL = kD * irradiance * albedo;
#endif

#if YA_DEFERRED_PBR_ENABLE_IBL_SPECULAR
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = uTexPBRPrefilterMap.SampleLevel(R, roughness * PBR_PREFILTER_MAX_REFLECTION_LOD).rgb;
    vec2 envBRDF = uTexPBRBrdfLUT.Sample(vec2(NdotV, roughness)).rg;
    specularIBL = prefilteredColor * (kS * envBRDF.x + envBRDF.y);
#endif

    vec3 ambient = (diffuseIBL + specularIBL) * ao;
    return ambient + Lo;
}

[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    float4 worldPos = mul(pc.modelMat, float4(IN.pos, 1.0));
    OUT.sv_position = mul(uFrame.projMat, mul(uFrame.viewMat, worldPos));
    OUT.worldPos = worldPos.xyz;
    OUT.uv = IN.uv;

    float3x3 normalMat = transpose(inverse3x3(float3x3(pc.modelMat)));
    float3 N = normalize(mul(normalMat, IN.normal));
    float3 T = normalize(mul(normalMat, IN.tangent));
    T = normalize(T - dot(T, N) * N);
    float3 B = normalize(cross(N, T));

    OUT.normal = N;
    OUT.tangent = T;
    OUT.bitangent = B;
}

struct FragmentOutput
{
    vec4 color : SV_Target;
};

[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    vec3 albedo = uParams.albedo * sampleTexture(uTexAlbedo, ETextureSlot::Albedo, IN.uv).rgb;
    float metallic = uParams.metallic * sampleTexture(uTexMetallic, ETextureSlot::Metallic, IN.uv).r;
    float roughness = uParams.roughness * sampleTexture(uTexRoughness, ETextureSlot::Roughness, IN.uv).r;
    float ao = uParams.ao * sampleTexture(uTexAO, ETextureSlot::AO, IN.uv).r;

    vec3 N = normalize(IN.normal);
    if (uParams.textures[(int)ETextureSlot::Normal].bEnable) {
        vec3 tangentNormal = sampleTexture(uTexNormal, ETextureSlot::Normal, IN.uv).xyz;
        tangentNormal = normalize(tangentNormal * 2.0 - 1.0);
        vec3 T = normalize(IN.tangent);
        vec3 B = normalize(IN.bitangent);
        N = normalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z);
    }

    vec3 V = normalize(uFrame.cameraPos - IN.worldPos);
    vec3 color = shadePBR(IN.worldPos, N, V, albedo, metallic, roughness, ao);
    OUT.color = vec4(color, 1.0);
}