#include "Common/Limits.slang"
#include "Common/Helper.slang"



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


struct FrameData{
    vec3 camPos;
};

struct DirectionalLight{
    vec3 dir;
    vec3 color;
    vec3 ambient;
    float shininess;
};

struct PointLight{
    vec3 pos;
    vec3 color;
    float intensity;
};

struct LightData{
    DirectionalLight dirLight;
    PointLight pointLights[MAX_POINT_LIGHTS];
    bool hasDirLight; // TODO: use static switch
    uint numPointLight;
};

// PBR material parameters — all packed in GBuffer:
//   RT0.a = roughness, RT1.a = ao, RT2.a = metallic
// PBRParams struct retained for reference / future per-pixel override.
struct PBRParams{
    vec3  albedo;
    float metallic;
    float roughness;
    float ao;
};


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

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}
float G_geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = G_geometrySchlickGGX(NdotV, roughness);
    float ggx1  = G_geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;

}

vec3 F_fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}




[[vk::binding(0,0)]] ConstantBuffer<FrameData,Std140DataLayout> uFrame;
[[vk::binding(1,0)]] ConstantBuffer<LightData,Std140DataLayout> uLight;

[[vk::binding(0,1)]] Sampler2D uTexGPos;
[[vk::binding(1,1)]] Sampler2D uTexGNormal;
[[vk::binding(2,1)]] Sampler2D uTexGAlbedoSpecular;

struct FragmentOutput{
    vec4 color : SV_Target;
};

[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    vec4 posRoughness = uTexGPos.Sample(IN.uv);
    vec4 normalAO     = uTexGNormal.Sample(IN.uv);
    vec4 albedoSpec   = uTexGAlbedoSpecular.Sample(IN.uv);

    vec3 worldPos = posRoughness.rgb;
    vec3 N        = normalize(normalAO.rgb);
    vec3 V        = normalize(uFrame.camPos - worldPos);

    // GBuffer layout:
    //   RT0 = (position.xyz, roughness)
    //   RT1 = (normal.xyz,   ao)
    //   RT2 = (albedo.rgb,   metallic)
    vec3  albedo    = albedoSpec.rgb;
    float metallic  = albedoSpec.a;
    float roughness = posRoughness.a;
    float ao        = normalAO.a;

    vec3 F0 = vec3(0.04);
    F0 = lerp(F0, albedo, metallic);

    // Reflectance equation
    vec3 Lo = vec3(0.0);

    // Directional light contribution
    if (uLight.hasDirLight)
    {
        vec3 L = normalize(-uLight.dirLight.dir);
        vec3 H = normalize(V + L);

        vec3 radiance = uLight.dirLight.color;

        float NDF = D_normalDistribution_GGX(N, H, roughness);
        float G   = G_geometrySmith(N, V, L, roughness);
        vec3 F    = F_fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        vec3  numerator   = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3  specular    = numerator / denominator;

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // Point light contributions
    for (int i = 0; i < uLight.numPointLight; ++i)
    {
        // per-light radiance
        vec3 L = normalize(uLight.pointLights[i].pos - worldPos);
        vec3 H = normalize(V + L);
        float distance    = length(uLight.pointLights[i].pos - worldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance     = uLight.pointLights[i].color * uLight.pointLights[i].intensity * attenuation;

        // cook-torrance brdf
        float NDF = D_normalDistribution_GGX(N, H, roughness);
        float G   = G_geometrySmith(N, V, L, roughness);
        vec3 F    = F_fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        vec3  numerator   = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.000001;
        vec3  specular    = numerator / denominator;

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    vec3 ambient = uLight.dirLight.ambient * albedo * ao;
    vec3 color = ambient + Lo;

    // HDR tone mapping (Reinhard)
    // color = color / (color + vec3(1.0));
    // Gamma correction
    // color = pow(color, vec3(1.0/2.2));

    OUT.color = vec4(color, 1.0);
}
