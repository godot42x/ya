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

// One sprite in the shared-quad instance buffer. Field order is the GPU
// record: the generated C++ header is the only copy of these offsets.
// `axisX`/`axisY` already carry the entity's rotation, scale and the
// component's size, so a far sprite looks smaller only because projection
// makes it smaller. Flip flags are baked into `uvRect` before upload.
//
// This struct is a push-constant parameter only so `slang_gen_header.py`
// emits the offsets. The vertex stage reads the `[[vk::location]]` arguments
// below, not `pc` (an unused push constant is dropped from SPIR-V). The
// pipeline binds those locations at `offsetof(SpriteInstance, field)` with
// instance rate. Do not delete `pc`: without it there is no generated type.
struct SpriteInstance
{
    float3 worldCenter;
    uint   textureIndex;
    float3 axisX;
    uint   _pad0;
    float3 axisY;
    uint   _pad1;
    // Atlas window (u0, v0, u1, v1).
    float4 uvRect;
    float4 tint;
};

[[vk::push_constant]] SpriteInstance pc;

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
SpriteVertexOutput vertWorldMain(
    SpriteVertexInput input,
    [[vk::location(3)]] float3 worldCenter,
    [[vk::location(4)]] uint   textureIndex,
    [[vk::location(5)]] float3 axisX,
    [[vk::location(6)]] float3 axisY,
    [[vk::location(7)]] float4 uvRect,
    [[vk::location(8)]] float4 tint)
{
    // The shared primitive quad spans [-1, 1]; the authored quad is `size` world
    // units across, so half of the primitive range is the quad-space corner.
    float2 corner   = input.pos.xy * 0.5;
    float3 worldPos = worldCenter + axisX * corner.x + axisY * corner.y;

    SpriteVertexOutput output;
    output.sv_position  = mul(uFrame.viewProj, float4(worldPos, 1.0));
    // uvRect is in image space (v0 = the image's top rows), while the shared
    // quad's texCoord.y grows toward +Y. The quad's top edge shows v0.
    float2 quadUv       = float2(input.texCoord.x, 1.0 - input.texCoord.y);
    output.texCoord     = lerp(uvRect.xy, uvRect.zw, quadUv);
    output.tint         = tint;
    output.textureIndex = textureIndex;
    return output;
}

[shader("fragment")]
float4 fragWorldMain(SpriteVertexOutput input) : SV_Target
{
    float4 color = uTextures[input.textureIndex].Sample(input.texCoord) * input.tint;
    // Early-out for texels that would not change the framebuffer. The pass
    // always blends (SrcAlpha / OneMinusSrcAlpha) and never writes depth.
    if (color.a < 0.01) {
        discard;
    }
    return color;
}
