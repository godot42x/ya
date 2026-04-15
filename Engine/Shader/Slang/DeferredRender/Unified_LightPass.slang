#include "Common/Limits.slang"
#include "Common/Helper.slang"

// Unified Light Pass — single pass with switch/case on Shading Model ID
// Reads from unified GBuffer:
//   RT0: (position.xyz, roughness)       R16G16B16A16_SFLOAT
//   RT1: (normal.xyz,   ao)              R16G16B16A16_SFLOAT
//   RT2: (albedo.rgb,   metallic/spec)   R8G8B8A8_UNORM
//   RT3: shadingModelID                  R8_UNORM (id/255.0)
//
// Shading Model IDs:
//   0 = background (no shading)
//   1 = PBR (Cook-Torrance BRDF)
//   2 = Phong (Blinn-Phong)

struct VertexInput{
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertexOutput{
    float4 sv_position : SV_Position;
    float2 uv : UV;
};

struct PushConstants
{
    float4x4 modelMat;
};
[[vk::push_constant]] PushConstants pc;


[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    OUT.sv_position = float4(IN.pos, 1.0);
    OUT.uv = IN.uv;
}


// ── UBO definitions ──

struct FrameData{
    vec3 camPos;
};

struct DirectionalLight{
    vec3 dir;
    vec3 color;
};

struct PointLight{
    vec3 pos;
    vec3 color;
    float intensity;
};

struct LightData{
    DirectionalLight dirLight;
    PointLight pointLights[MAX_POINT_LIGHTS];
    vec3 ambient;
    bool hasDirLight;
    uint numPointLight;
};


// ── PBR BRDF functions ──

float D_normalDistribution_GGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float G_geometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
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


// ── Shading functions ──

// MARK: PBR
vec3 shadePBR(in vec3 worldPos,in vec3 N, in vec3 V, in vec3 albedo, float metallic, float roughness, float ao)
{
    vec3 F0 = lerp(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    // Directional light
    if (uLight.hasDirLight)
    {
        // per-light radiance
        vec3 L = normalize(-uLight.dirLight.dir);
        vec3 H = normalize(V + L);
        vec3 radiance = uLight.dirLight.color; // raw light

        // Cook-Torrance BRDF
        float NDF = D_normalDistribution_GGX(N, H, roughness);
        float G   = G_geometrySmith(N, V, L, roughness);
        vec3 F    = F_fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);

        Lo += (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
    }

    // Point lights
    for (int i = 0; i < uLight.numPointLight; ++i)
    {
        // per-light radiance
        vec3 L = normalize(uLight.pointLights[i].pos - worldPos);
        vec3 H = normalize(V + L);
        float dist = length(uLight.pointLights[i].pos - worldPos);
        // raw light
        vec3 radiance = uLight.pointLights[i].color * uLight.pointLights[i].intensity 
                        / (dist * dist);

        // Cook-Torrance BRDF
        float NDF = D_normalDistribution_GGX(N, H, roughness);
        float G   = G_geometrySmith(N, V, L, roughness);
        vec3 F    = F_fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD =  vec3(1.0) - kS; // 能量守恒, 除非自发光
        kD =  kD *(1.0 - metallic);

        vec3 numerator = NDF* G* F;
        float denominator = 4.0 * max(dot(N,V), 0.0) * max(dot(N,L), 0.0) + 0.000001;
        vec3 specular = numerator / denominator;

        Lo += (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
    }

    // ambient lighting(IBL)
    // vec3 kS = F_fresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS = F_fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = vec3(1.0)- kS;
    kD *= 1.0 - metallic;
    vec3 irradiance = uTexIrradianceCubemap.Sample(N).rgb;
    vec3 diffuse  = irradiance * albedo;
    vec3 ambient    = (kD * diffuse) * ao;

    return ambient + Lo;
}

// MARK: Phong
vec3 shadePhong(vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float specIntensity, float shininess , float reflectRatio)
{
    vec3 light = 0;

    vec3 R =  vec3(0);
    if (reflectRatio>0)
    {
        vec3 I = normalize(V);
        vec3 reflectDir = reflect(I, N);
        R = uTexSkyBoxMap.Sample(reflectDir).xyz;
        R *= specIntensity;
    }

    // Directional light — Blinn-Phong
    if(uLight.hasDirLight){
        vec3 lightDir   = normalize(-uLight.dirLight.dir);
        float diff      = max(dot(lightDir, N), 0.0);
        vec3 halfwayDir = normalize(lightDir + V);
        float spec      = pow(max(dot(N, halfwayDir), 0.0),shininess);

        vec3 ambient  = albedo * uLight.ambient;
        vec3 diffuse  = diff * albedo * uLight.dirLight.color;
        vec3 specular = specIntensity * spec * uLight.dirLight.color;
        light += ambient + diffuse + specular;
    }

    for(uint i = 0; i< uLight.numPointLight; ++i)
    {
        vec3  lightDir   = normalize(uLight.pointLights[i].pos - worldPos);
        float diff      = max(dot(lightDir, N), 0.0);
        vec3  halfwayDir = normalize(lightDir + V);
        float spec      = pow(max(dot(N, halfwayDir), 0.0),shininess);

        float dist = length(uLight.pointLights[i].pos - worldPos);
        vec3 radiance = uLight.pointLights[i].color * uLight.pointLights[i].intensity / (dist * dist);

        vec3 ambient  = albedo * uLight.ambient;
        vec3 diffuse  = diff * albedo * radiance;
        vec3 specular = specIntensity * spec * radiance;
        light += ambient + diffuse + specular;
    }


    return light + R;
}


// ── Descriptor bindings ──

[[vk::binding(0,0)]] ConstantBuffer<FrameData,Std140DataLayout> uFrame;
[[vk::binding(1,0)]] ConstantBuffer<LightData,Std140DataLayout> uLight;

[[vk::binding(0,1)]] Sampler2D uTexRT0; // RT0: (position.xyz, roughness/shininess)
[[vk::binding(1,1)]] Sampler2D uTexRT1; // RT1: (normal.xyz, ao/reflectRatio)
[[vk::binding(2,1)]] Sampler2D uTexRT2; // RT2: (albedo.rgb, metallic/specIntensity)
[[vk::binding(3,1)]] Sampler2D uTexRT3; // RT3: shadingModelID

[[vk::binding(0,2)]] SamplerCube uTexSkyBoxMap; // skybox -> preFilteredEnvironmentMap
[[vk::binding(1,2)]] SamplerCube uTexIrradianceCubemap; 
[[vk::binding(2,2)]] SamplerCube uTexEnvironmentMap;  // pbr: prefilter map lut
[[vk::binding(3,2)]] Sampler2D uTexBrdfLUT;  



struct FragmentOutput{
    vec4 color : SV_Target;
};

[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    // RT3 is the branch selector; keep it outside so the remaining RT fetches stay per-model.
    float smRaw = uTexRT3.Sample(IN.uv).r;
    uint shadingModelID = uint(smRaw * 255.0 + 0.5);

    vec4 rt0 = uTexRT0.Sample(IN.uv);
    vec4 rt1 = uTexRT1.Sample(IN.uv);
    vec4 rt2 = uTexRT2.Sample(IN.uv);

    vec3 color = vec3(0.0);

    switch (shadingModelID) {
    case 1: // PBR
    {
        vec3 worldPos = rt0.rgb;
        vec3 N        = normalize(rt1.rgb);
        vec3 V        = normalize(uFrame.camPos - worldPos);
        vec3 albedo   = rt2.rgb;

        float metallic  = rt2.a;
        float roughness = rt0.a;
        float ao        = rt1.a;
        color = shadePBR(worldPos, N, V, albedo, metallic, roughness, ao);
        break;
    }
    case 2: // Phong
    {
        vec3 worldPos = rt0.rgb;
        vec3 N        = normalize(rt1.rgb);
        vec3 V        = normalize(uFrame.camPos - worldPos);
        vec3 albedo   = rt2.rgb;

        float shininess     = rt0.a;
        float specIntensity = rt2.a;
        float reflectRatio  = rt1.a;
        color = shadePhong(worldPos, N, V, albedo, specIntensity, shininess, reflectRatio);
        break;
    }
    case 3: // Unlit — direct albedo output, no lighting
    {
        vec4 rt2 = uTexRT2.Sample(IN.uv);
        color = rt2.rgb;
        break;
    }
    default:
        // ID=0: background — no shading, keep black
        discard;
    }

    OUT.color = vec4(color, 1.0);
}
