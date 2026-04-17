#include "Common/Helper.slang"

// Unified GBuffer Pass — Unlit variant
// GBuffer layout (shared with PBR/Phong):
//   RT0: (position.xyz, 0.0)            R16G16B16A16_SFLOAT
//   RT1: (normal.xyz,   0.0)            R16G16B16A16_SFLOAT
//   RT2: (finalColor.rgb, 0.0)          R8G8B8A8_UNORM
//   RT3: shadingModelID                  R8_UNORM (id/255.0)
//
// Unlit writes the final computed color into albedo; Light Pass outputs it as-is.

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
};

struct PushConstants
{
    float4x4 modelMat;
};

[[vk::push_constant]] PushConstants pc;

// Set 0: Frame + Light UBO (shared)
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

// Set 1: Unlit material textures (2 slots)
[[vk::binding(0, 1)]]  Sampler2D  uTexBaseColor0;
[[vk::binding(1, 1)]]  Sampler2D  uTexBaseColor1;

// Set 2: Unlit material params UBO
struct TextureParams{
    bool bEnable;
    mat3 uvTransform;
};

struct ParamsData{
    vec3  baseColor0;
    vec3  baseColor1;
    float mixValue;
    TextureParams textures[2];
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

    float3x3 normalMat = transpose(inverse3x3(float3x3(pc.modelMat)));
    OUT.normal = mul(normalMat, IN.normal);
}


// MARK: Fragment

struct FragmentOutput{
    float4 posRoughness    : SV_Target0;  // (position.xyz, 0.0)
    float4 normalAO        : SV_Target1;  // (normal.xyz,   0.0)
    float4 albedoMisc      : SV_Target2;  // (finalColor.rgb, 0.0)
    float  shadingModelID  : SV_Target3;  // shading model ID (3=Unlit)
};


[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    // Compute final unlit color: mix two base colors, modulated by textures
    vec3 color0 = uParams.baseColor0;
    vec3 color1 = uParams.baseColor1;

    if (uParams.textures[0].bEnable) {
        vec2 uv0 = (uParams.textures[0].uvTransform * vec3(IN.uv, 1.0)).xy;
        color0 = uTexBaseColor0.Sample(uv0).rgb;
    }
    if (uParams.textures[1].bEnable) {
        vec2 uv1 = (uParams.textures[1].uvTransform * vec3(IN.uv, 1.0)).xy;
        color1 = uTexBaseColor1.Sample(uv1).rgb;
    }

    vec3 finalColor = lerp(color0, color1, uParams.mixValue);

    // Pack into GBuffer — Unlit: no lighting needed, store final color as albedo
    OUT.posRoughness   = vec4(IN.pos, 0.0);
    OUT.normalAO       = vec4(normalize(IN.normal), 0.0);
    OUT.albedoMisc     = vec4(finalColor, 0.0);
    OUT.shadingModelID = 3.0 / 255.0; // EShadingModelID::Unlit = 3
}
