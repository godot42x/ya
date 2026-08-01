#ifndef TEXTURE_SET_SIZE
#define TEXTURE_SET_SIZE 16
#endif

// Set 0: Frame UBO
struct FrameData
{
    float4x4 viewProjection;
    float4x4 view;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

// Push Constants
struct PushConstants
{
    float3 worldCenter;
    uint   textureIndex;
    float3 worldDirection;
    uint   _pad0;
    float2 worldSize;
    float2 _pad1;
    float4 tint;
};

[[vk::push_constant]] PushConstants pc;

// Set 1: Texture array
[[vk::binding(0, 1)]] Sampler2D uTextures[TEXTURE_SET_SIZE];

// Vertex Input
struct VertexInput
{
    [[vk::location(0)]] float3 pos      : POSITION;
    [[vk::location(1)]] float2 texCoord : TexCoord;
    [[vk::location(2)]] float3 normal   : NORMAL;
};

// Vertex-to-Fragment
struct VertexOutput
{
    float4 sv_position                  : SV_Position;
    float2 texCoord                     : TexCoord;
    float4 tint                         : COLOR0;
    nointerpolation uint textureIndex   : TextureIdx;
    nointerpolation float3 worldDirection : WorldDir;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    float3 cameraRight = normalize(uFrame.view[0].xyz);
    float3 cameraUp    = normalize(uFrame.view[1].xyz);
    float2 centered    = input.pos.xy * 0.5 * pc.worldSize;
    float3 worldPos    = pc.worldCenter + cameraRight * centered.x + cameraUp * centered.y;

    VertexOutput output;
    output.sv_position    = mul(uFrame.viewProjection, float4(worldPos, 1.0));
    output.texCoord       = input.texCoord;
    output.tint           = pc.tint;
    output.textureIndex   = pc.textureIndex;
    output.worldDirection = pc.worldDirection;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float4 texColor = uTextures[input.textureIndex].Sample(input.texCoord);
    float4 color = texColor * input.tint;
    if (color.a < 0.01) {
        discard;
    }
    return color;
}
