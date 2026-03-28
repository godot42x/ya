#include "Common/Helper.slang"

// Unified GBuffer Pass — PBR variant
// GBuffer layout:
//   RT0: (position.xyz, roughness)    R16G16B16A16_SFLOAT
//   RT1: (normal.xyz,   ao)           R16G16B16A16_SFLOAT
//   RT2: (albedo.rgb,   metallic)     R8G8B8A8_UNORM
//   D+S: depth + stencil (ref=1 for PBR)

struct VertexInput{
    [[vk::location(0)]] float3 pos      : POSITION;
    [[vk::location(1)]] float2 uv       : TEXCOORD0;
    [[vk::location(2)]] float3 normal   : NORMAL;
    [[vk::location(3)]] float3 tangent  : TANGENT;
};

struct VertexOutput{
    float4 sv_position: SV_Position;
    float3 pos :Position;
    float2 uv   :UV;
    float3 normal :Normal;
    float3x3 TBN;
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

[[vk::binding(0,0)]] ConstantBuffer<FrameData,Std140DataLayout> uFrame;
[[vk::binding(1,0)]] ConstantBuffer<LightData,Std140DataLayout> uLight;

// Set 1: PBR material textures (5 slots)
[[vk::binding(0, 1)]]  Sampler2D  uTexAlbedo;
[[vk::binding(1, 1)]]  Sampler2D  uTexNormal;
[[vk::binding(2, 1)]]  Sampler2D  uTexMetallic;
[[vk::binding(3, 1)]]  Sampler2D  uTexRoughness;
[[vk::binding(4, 1)]]  Sampler2D  uTexAO;

// Set 2: PBR material params UBO
struct PBRParamsData{
    vec3  albedo;
    float metallic;
    float roughness;
    float ao;
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

    // TODO: support normal map texture, by calculate TBN then pass to fragments
    float3x3 normalMat = transpose(inverse3x3(float3x3(pc.modelMat)));
    OUT.normal =  mul(normalMat, IN.normal);
}


// MARK: Fragment

struct FragmentOutput{
    float4 posRoughness    : SV_Target0;  // (position.xyz, roughness)
    float4 normalAO        : SV_Target1;  // (normal.xyz,   ao)
    float4 albedoMetallic  : SV_Target2;  // (albedo.rgb,   metallic)
    float  shadingModelID  : SV_Target3;  // shading model ID (1=PBR, encoded as id/255.0)
};


[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    // Albedo from texture modulated by UBO albedo color
    vec3  albedo    = uParams.albedo * uTexAlbedo.Sample(IN.uv).rgb;
    float metallic  = uParams.metallic  * uTexMetallic.Sample(IN.uv).r;
    float roughness = uParams.roughness * uTexRoughness.Sample(IN.uv).r;
    float ao        = uParams.ao        * uTexAO.Sample(IN.uv).r;

    // Pack into GBuffer — no extra RT, roughness/ao ride the unused alpha channels
    OUT.posRoughness   = vec4(IN.pos, roughness);
    OUT.normalAO       = vec4(normalize(IN.normal), ao);
    OUT.albedoMetallic = vec4(albedo, metallic);
    OUT.shadingModelID = 1.0 / 255.0; // EShadingModelID::PBR = 1
}
