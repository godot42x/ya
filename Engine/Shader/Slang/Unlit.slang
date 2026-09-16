// Forward-pass unlit material.
//
// Ported from Engine/Shader/GLSL/Test/Unlit.glsl. Bindings, push constants and
// the std140 layout of MaterialUBO are unchanged so UnlitMaterial's
// static_asserts still cross-check against the generated companion header.

#include "Common/Helper.slang"
#include "Common/Skinning.slang"

// Set 0: frame matrices (every frame, once).
struct FrameUBO
{
    float4x4 projMat;
    float4x4 viewMat;
    int2     resolution;
    uint     frameIdx;
    float    time;
};

// Set 1: material parameters (every material update, once).
struct TextureParam
{
    bool     bEnable;
    mat3     uvTransform;
};

struct MaterialUBO
{
    float3       baseColor0;
    float3       baseColor1;
    float        mixValue;
    TextureParam textureParams[2];
};

struct PushConstants
{
    float4x4 modelMat;
    int      skinningPaletteIndex;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameUBO, Std140DataLayout>    uFrame;
[[vk::binding(0, 1)]] ConstantBuffer<MaterialUBO, Std140DataLayout> uMaterial;

// Set 2: the two blend textures.
[[vk::binding(0, 2)]] Sampler2D uTexture0;
[[vk::binding(1, 2)]] Sampler2D uTexture1;

[[vk::push_constant]] PushConstants pc;

struct VertexInput
{
    [[vk::location(0)]] float3 pos      : POSITION;
    [[vk::location(1)]] float2 texcoord : TEXCOORD_UV;
    [[vk::location(2)]] float3 normal   : NORMAL;
    YA_SKINNING_VERTEX_FIELDS
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    [[vk::location(1)]] float2 texcoord : TEXCOORD_UV;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;

    float4 localPos = float4(input.pos, 1.0);

#if ENABLE_SKINNING
    // The forward unlit pass shades from the base colours and textures only, so
    // the skinned normal is computed for consistency but not carried to the
    // fragment stage.
    localPos = applySkinning(input.pos,
                             input.normal,
                             float3(0.0, 0.0, 0.0),
                             pc.skinningPaletteIndex,
                             input.boneIDs,
                             input.weights)
                   .localPos;
#endif

    output.sv_position = mul(uFrame.projMat, mul(uFrame.viewMat, mul(pc.modelMat, localPos)));
    output.texcoord    = input.texcoord;
    return output;
}

inline float2 applyTextureUV(TextureParam param, float2 uv)
{
    return mul(param.uvTransform, float3(uv, 1.0)).xy;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float3 color0 = uMaterial.baseColor0;
    float3 color1 = uMaterial.baseColor1;

    if (uMaterial.textureParams[0].bEnable)
    {
        color0 = uTexture0.Sample(applyTextureUV(uMaterial.textureParams[0], input.texcoord)).rgb;
    }
    if (uMaterial.textureParams[1].bEnable)
    {
        color1 = uTexture1.Sample(applyTextureUV(uMaterial.textureParams[1], input.texcoord)).rgb;
    }

    return float4(lerp(color0, color1, uMaterial.mixValue), 1.0);
}
