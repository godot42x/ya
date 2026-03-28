#include "Common/Helper.slang"

// Unified GBuffer Pass — Phong variant
// GBuffer layout (shared with PBR):
//   RT0: (position.xyz, roughness=0.0)   R16G16B16A16_SFLOAT
//   RT1: (normal.xyz,   ao=1.0)          R16G16B16A16_SFLOAT
//   RT2: (albedo.rgb,   specular)        R8G8B8A8_UNORM
//   D+S: depth + stencil (ref=2 for Phong)
//
// Phong does not use roughness/ao; alpha channels filled with defaults.

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

// Set 0: Frame + Light UBO (shared with PBR)
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

// Set 1: Phong material textures
[[vk::binding(0, 1)]]  Sampler2D  uTexDiffuse;
[[vk::binding(1, 1)]]  Sampler2D  uTexSpecular;
[[vk::binding(2, 1)]]  Sampler2D  uTexNormal;

// Set 2: Phong material params UBO
public struct TextureParams{
    bool bEnable;
    mat3 uvTransform;
};

struct ParamsData{
    TextureParams textures[ETextureSlot::Count];
};

[[vk::binding(0, 2)]] ConstantBuffer<ParamsData,Std140DataLayout> uParams;


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
    float4 posRoughness      : SV_Target0;  // (position.xyz, roughness=0.0)
    float4 normalAO          : SV_Target1;  // (normal.xyz,   ao=1.0)
    float4 albedoSpecular    : SV_Target2;  // (albedo.rgb,   specular)
    float  shadingModelID    : SV_Target3;  // shading model ID (2=Phong, encoded as id/255.0)
};


[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    // Position — roughness unused in Phong, fill 0.0
    OUT.posRoughness = vec4(IN.pos, 0.0);

    // Normal — ao unused in Phong, fill 1.0 (no darkening)
    OUT.normalAO = vec4(normalize(IN.normal), 1.0);

    // Albedo + specular intensity
    float4 albedoSpec = float4(1.0);
    if(uParams.textures[ETextureSlot::Diffuse].bEnable){
        albedoSpec.rgb = uTexDiffuse.Sample(IN.uv).rgb;
    }
    if(uParams.textures[ETextureSlot::Specular].bEnable){
        albedoSpec.a = uTexSpecular.Sample(IN.uv).r;
    }
    OUT.albedoSpecular = albedoSpec;
    OUT.shadingModelID = 2.0 / 255.0; // EShadingModelID::Phong = 2
}
