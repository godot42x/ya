#include "Common/Helper.slang"
#include "DeferredRender/Types.slang"

// Unified GBuffer Pass — PBR variant
// GBuffer layout:
//   RT0: (position.xyz, roughness)    R16G16B16A16_SFLOAT
//   RT1: (normal.xyz,   ao)           R16G16B16A16_SFLOAT
//   RT2: (albedo.rgb,   metallic)     R8G8B8A8_UNORM
//   D+S: depth + stencil (ref=1 for PBR)

#ifndef ENABLE_SKINNING
    #define ENABLE_SKINNING 0
#endif


struct VertexInput{
    [[vk::location(0)]] float3 pos      : POSITION;
    [[vk::location(1)]] float2 uv       : TEXCOORD0;
    [[vk::location(2)]] float3 normal   : NORMAL;
    [[vk::location(3)]] float3 tangent  : TANGENT;
#if ENABLE_SKINNING
    [[vk::location(4)]] vector<int32_t, MAX_BONE_WEIGHT_PER_VERTEX> boneIDs: BONE_IDS;
    [[vk::location(5)]] vector<float,MAX_BONE_WEIGHT_PER_VERTEX>  weights    : WEIGHTS;
#endif
};

struct VertexOutput{
    float4 sv_position: SV_Position;
    [[vk::location(0)]] float3 pos :Position;
    [[vk::location(1)]] float2 uv   :UV;
    [[vk::location(2)]] float3 normal :Normal;
    [[vk::location(3)]] float3 tangent :Tangent;
    [[vk::location(4)]] float3 bitangent :Bitangent;
};

struct PushConstants
{
    float4x4 modelMat;
};

[[vk::push_constant]] PushConstants pc;

// Set 0: Frame + Light UBO
struct FrameData{
    float3 viewPos;
    mat4  viewMatrix;
    mat4  projMatrix;
};

struct DirectionalLight{
    vec3 dir;
    vec3 color;
    vec3 ambient;
    float shininess;
};

struct LightData{
    DirectionalLight dirLight;
};


struct AnimationData{
    vector<vec4, MAX_BONE_COUNT> boneOffsets;
};

[[vk::binding(0,0)]] ConstantBuffer<FrameData,Std140DataLayout> uFrame;
[[vk::binding(1,0)]] ConstantBuffer<LightData,Std140DataLayout> uLight;


enum ETextureSlot{
   Albedo = 0,
   Normal = 1,
   Metallic = 2,
   Roughness = 3,
   AO = 4,
   Count = 5,
};

// Set 1: PBR material textures (5 slots)
[[vk::binding(0, 1)]]  Sampler2D  uTexAlbedo;
[[vk::binding(1, 1)]]  Sampler2D  uTexNormal;
[[vk::binding(2, 1)]]  Sampler2D  uTexMetallic;
[[vk::binding(3, 1)]]  Sampler2D  uTexRoughness;
[[vk::binding(4, 1)]]  Sampler2D  uTexAO;

struct TextureParams{
    bool bEnable;
    float rotationRadius;
    vec2  translation;
    vec2  scale;
};

// Set 2: PBR material params UBO
struct PBRParamsData{
    vec3  albedo;
    float metallic;
    float roughness;
    float ao;
    TextureParams textures[ETextureSlot::Count];
};
[[vk::binding(0, 2)]] ConstantBuffer<PBRParamsData,Std140DataLayout> uParams;


[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    float4 worldPos = pc.modelMat * float4(IN.pos, 1.0);
    float4 clipPos = uFrame.projMatrix * uFrame.viewMatrix * worldPos;

    OUT.sv_position = clipPos;
    OUT.pos = worldPos.xyz;
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


// MARK: Fragment

struct FragmentOutput{
    float4 posRoughness    : SV_Target0;  // (position.xyz, roughness)
    float4 normalAO        : SV_Target1;  // (normal.xyz,   ao)
    float4 albedoMetallic  : SV_Target2;  // (albedo.rgb,   metallic)
    float  shadingModelID  : SV_Target3;  // shading model ID (1=PBR, encoded as id/255.0)
};


vec2 getUV(ETextureSlot slot, vec2 uv)
{
    TextureParams  params = uParams.textures[(int)slot];
    uv +=   params.translation;
    float c = cos(params.rotationRadius);
    float s = sin(params.rotationRadius);
    uv = vec2 (
        uv.x * c - uv.y * s,
        uv.x * s + uv.y * c
    );
    uv *= params.scale;
    return uv;
}

vec4 sample2DTextureConditionally(in Sampler2D tex, ETextureSlot slot, vec2 uv)
{
    if(!uParams.textures[(int)slot].bEnable){
        return vec4(1.0); // default value when texture is disabled
    }

    return  tex.Sample(getUV(slot, uv));
}


[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    vec3  albedo    = uParams.albedo * sample2DTextureConditionally(uTexAlbedo, ETextureSlot::Albedo, IN.uv).rgb;
    float metallic  = uParams.metallic * sample2DTextureConditionally(uTexMetallic, ETextureSlot::Metallic, IN.uv).r;
    float roughness = uParams.roughness * sample2DTextureConditionally(uTexRoughness, ETextureSlot::Roughness, IN.uv).r;
    float ao        = uParams.ao * sample2DTextureConditionally(uTexAO, ETextureSlot::AO, IN.uv).r;
    vec3  N         = normalize(IN.normal);

    // normal map
    if (uParams.textures[(int)ETextureSlot::Normal].bEnable) {
        vec3 normalMap = sample2DTextureConditionally(uTexNormal, ETextureSlot::Normal, IN.uv).xyz;
        normalMap = normalize(normalMap * 2.0 - 1.0);
        vec3 T = normalize(IN.tangent);
        vec3 B = normalize(IN.bitangent);
        N = normalize(T * normalMap.x + B * normalMap.y + N * normalMap.z);
    }

    // Pack into GBuffer — no extra RT, roughness/ao ride the unused alpha channels
    OUT.posRoughness   = vec4(IN.pos, roughness);
    OUT.normalAO       = vec4(N, ao);
    OUT.albedoMetallic = vec4(albedo, metallic);
    OUT.shadingModelID = 1.0 / 255.0; // EShadingModelID::PBR = 1
}
