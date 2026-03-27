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
    // OUT.sv_position = uFrame.projMatrix * uFrame.viewMatrix * float4(IN.pos, 1.0);
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
};

struct LightData{
    DirectionalLight dirLight;
    PointLight pointLights[MAX_POINT_LIGHTS];
    bool hasDirLight; // TODO: use static switch
    uint numPointLight;
};

// Static switch params
#ifndef HAS_DIR_LIGHT
    #define HAS_DIR_LIGHT 0
#endif


struct PBRParams{
    // material parameters
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

vec3 F_fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness);
vec3 F_fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}





[[vk::binding(0,0)]] ConstantBuffer<FrameData,Std140DataLayout> uFrame;
[[vk::binding(1,0)]] ConstantBuffer<LightData,Std140DataLayout> uLight;

[[vk::binding(0,1)]] Sampler2D uTexGPos;
[[vk::binding(1,1)]] Sampler2D uTexGNormal;
[[vk::binding(2,1)]] Sampler2D uTexGAlbedoSpecular;

[[vk::binding(0,2)]] PBRParams uParams;


struct FragmentOutput{
    vec4 color : SV_Target;
}

void main(in VertexOutput IN, out FragmentOutput OUT)
{       
    vec3 worldPos = uTexGPos.Sample(IN.uv).rgb;
    vec3 normal = uTexGNormal.Sample(IN.uv).rgb;
    vec4 albedoSpecular = uTexGAlbedoSpecular.Sample(IN.uv);

    vec3 N = normalize(normal);
    vec3 V = normalize(uFrame.camPos - worldPos);

    vec3 albedo = albedoSpecular.rgb * uParams.albedo;

    vec3 F0 = vec3(0.04); 
    F0 = lerp(F0, albedo, uParams.metallic);

    // reflectance equation
    vec3 Lo = vec3(0.0);
    for(int i = 0; i < uLight.numPointLight; ++i) 
    {
        // calculate per-light radiance
        vec3 L = normalize(uLight.pointLights[i].pos - worldPos);
        vec3 H = normalize(V + L);
        float distance    = length(uLight.pointLights[i].pos - worldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance     = uLight.pointLights[i].color * attenuation;        

        // cook-torrance brdf
        float NDF = D_normalDistribution_GGX(N, H, uParams.roughness);        
        float G   = G_geometrySmith(N, V, L, uParams.roughness);      
        vec3 F    = F_fresnelSchlick(max(dot(H, V), 0.0), F0);       

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - uParams.metallic;     

        vec3  nominator    = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; 
        vec3  specular     = nominator / denominator;

        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0);                
        Lo += (kD * albedo / PI + specular) * radiance * NdotL; 
    }   

    vec3 ambient = vec3(0.03) * albedo * uParams.ao;
    vec3 color = ambient + Lo;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));  

    OUT.color = vec4(color, 1.0);
}  