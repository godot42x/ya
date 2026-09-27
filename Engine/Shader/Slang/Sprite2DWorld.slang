// Scene sprite shader (world space). Draws the authored `Sprite2DComponent`
// candidates of a scene graph: quads that lie on the entity's own local XY
// plane, so rotating the entity turns the quad (the Unity-style world quad).
//
// A camera-facing quad is a billboard, not a sprite, and belongs to
// Misc/BillboardWorld.slang. Screen-space GUI compose (UIFrameSnapshot replay)
// uses Sprite2DScreen.slang; debug world lines use Sprite2DLine.slang.
//
// No SDF branches here: glyph text and round-rect SDF are screen-space UI
// primitives (see Sprite2DScreen.slang).

#include "Common/Limits.slang"

#ifndef TEXTURE_SET_SIZE
#define TEXTURE_SET_SIZE 16
#endif

struct FrameData
{
    float4x4 viewProj;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

[[vk::binding(0, 1)]] Sampler2D uTextures[TEXTURE_SET_SIZE];

// Per-sprite draw data. The quad mesh is shared by every sprite, so the
// authored placement rides here: the world center plus the two world axes the
// quad is expanded along. `axisX`/`axisY` already carry the entity's rotation,
// scale and the component's size, so a far sprite looks smaller only because
// projection makes it smaller.
struct SpritePushConstant
{
    float3 worldCenter;
    uint   textureIndex;
    float3 axisX;
    uint   _pad0;
    float3 axisY;
    uint   _pad1;
    // Atlas window (u0, v0, u1, v1); the component's flip flags are baked in by
    // the extraction step.
    float4 uvRect;
    float4 tint;
};

[[vk::push_constant]] SpritePushConstant pc;

struct SpriteVertexInput
{
    [[vk::location(0)]] float3 pos      : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD;
    [[vk::location(2)]] float3 normal   : NORMAL;
};

struct SpriteVertexOutput
{
    float4 sv_position : SV_Position;
    float2 texCoord    : TEXCOORD;
    float4 tint        : COLOR0;
    nointerpolation uint textureIndex : TEXCOORD_TEXTURE_SLOT;
};

[shader("vertex")]
SpriteVertexOutput vertWorldMain(SpriteVertexInput input)
{
    // The shared primitive quad spans [-1, 1]; the authored quad is `size` world
    // units across, so half of the primitive range is the quad-space corner.
    float2 corner   = input.pos.xy * 0.5;
    float3 worldPos = pc.worldCenter + pc.axisX * corner.x + pc.axisY * corner.y;

    SpriteVertexOutput output;
    output.sv_position  = mul(uFrame.viewProj, float4(worldPos, 1.0));
    output.texCoord     = lerp(pc.uvRect.xy, pc.uvRect.zw, input.texCoord);
    output.tint         = pc.tint;
    output.textureIndex = pc.textureIndex;
    return output;
}

[shader("fragment")]
float4 fragWorldMain(SpriteVertexOutput input) : SV_Target
{
    float4 color = uTextures[input.textureIndex].Sample(input.texCoord) * input.tint;
    // An opaque sprite is cut out by its own texture rather than blended: the
    // pass binds it without blending and writes depth, so dropping fully
    // transparent texels here is what keeps its silhouette clean.
    if (color.a < 0.01) {
        discard;
    }
    return color;
}
