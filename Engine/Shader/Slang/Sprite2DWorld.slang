// Scene 2D shader (world-space): world billboards expanded on the camera
// right/up axes. Owned by the scene side -- the editor's world overlays and
// the Phase 4 World2D workload sample it; the GUI compose paths (UIFrameSnapshot
// replay) use Sprite2DScreen.slang. Debug world lines live in
// Sprite2DLine.slang (the only 2D pipeline that depth-tests).
//
// No SDF branches here: glyph text and round-rect SDF are screen-space UI
// primitives (see Sprite2DScreen.slang).

#include "Common/Limits.slang"

#ifndef TEXTURE_SET_SIZE
#define TEXTURE_SET_SIZE 16
#endif

#ifndef SAMPLE_MODE_COVERAGE
#define SAMPLE_MODE_COVERAGE 0
#endif

#ifndef SAMPLE_MODE_OPAQUE
#define SAMPLE_MODE_OPAQUE 2
#endif

struct FrameData
{
    float4x4 viewProj;
    float4x4 view;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

[[vk::binding(0, 1)]] Sampler2D uTextures[TEXTURE_SET_SIZE];

struct VertexInput
{
    [[vk::location(0)]] float3 pos         : POSITION;
    [[vk::location(1)]] float4 color       : COLOR0;
    [[vk::location(2)]] float2 texCoord    : TEXCOORD;
    [[vk::location(3)]] uint   textureSlot : TEXCOORD_TEXTURE_SLOT;
    [[vk::location(4)]] uint   sampleMode  : TEXCOORD_SAMPLE_MODE;
    [[vk::location(5)]] float3 worldCenter : TEXCOORD_WORLD_CENTER;
    [[vk::location(6)]] float3 worldDirection : TEXCOORD_WORLD_DIR;
    [[vk::location(7)]] float2 worldSize   : TEXCOORD_WORLD_SIZE;
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float4 color       : COLOR0;
    float2 texCoord    : TEXCOORD;
    nointerpolation uint textureSlot : TEXCOORD_TEXTURE_SLOT;
    nointerpolation uint sampleMode  : TEXCOORD_SAMPLE_MODE;
};

float2 flipWorldUv(float2 uv)
{
    return float2(uv.x, 1.0 - uv.y);
}

[shader("vertex")]
VertexOutput vertWorldMain(VertexInput input)
{
    float3 cameraRight = normalize(uFrame.view[0].xyz);
    float3 cameraUp    = normalize(uFrame.view[1].xyz);
    float2 centered    = (input.pos.xy - float2(0.5, 0.5)) * input.worldSize;
    float3 worldPos    = input.worldCenter + cameraRight * centered.x + cameraUp * centered.y;

    VertexOutput output;
    output.sv_position = mul(uFrame.viewProj, float4(worldPos, 1.0));
    output.color       = input.color;
    output.texCoord    = flipWorldUv(input.texCoord);
    output.textureSlot = input.textureSlot;
    output.sampleMode  = input.sampleMode;
    return output;
}

[shader("fragment")]
float4 fragWorldMain(VertexOutput input) : SV_Target
{
    const uint textureIdx = input.textureSlot;
    float4 texColor = uTextures[textureIdx].Sample(input.texCoord);
    // Viewport / scene RTs often leave alpha unused (0). Discard would punch
    // the whole sprite to the background. The opaque sample mode requests
    // opaque RGB.
    if (input.sampleMode == SAMPLE_MODE_OPAQUE) {
        texColor.a = 1.0;
    }
    else if (texColor.a < 0.01) {
        discard;
    }
    return texColor * input.color;
}
