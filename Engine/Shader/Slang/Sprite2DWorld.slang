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
#include "Common/VertexInput.slang"

#ifndef TEXTURE_SET_SIZE
#define TEXTURE_SET_SIZE 16
#endif

struct FrameData
{
    float4x4 viewProj;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

[[vk::binding(0, 1)]] Sampler2D uTextures[TEXTURE_SET_SIZE];

// One sprite in the shared-quad instance buffer. [YaVertexInput] is what
// asks the header generator for the C++ record and SpriteInstanceFields
// (SPIR-V location, byte offset, components). `axisX`/`axisY` already carry
// the entity's rotation, scale and the component's size, so a far sprite
// looks smaller only because projection makes it smaller. Flip flags are
// baked into `uvRect` before upload.
//
// [[vk::location(3)]] on the parameter is the SPIR-V base. Field locations in
// reflection are relative to it (0..5 here), and the generated table stores
// base + field index, which is the SPIR-V location 3-8. Writing those
// locations on the fields instead shifts SPIR-V by this parameter's base
// (the mesh inputs occupy 0-2, so field location 3 becomes SPIR-V 6).
// Instance rate is the pipeline binding (binding 1), not a SPIR-V decoration.
[YaVertexInput]
struct SpriteInstance
{
    float3 worldCenter;
    uint   textureIndex;
    float3 axisX;
    float3 axisY;
    // Atlas window (u0, v0, u1, v1).
    float4 uvRect;
    float4 tint;
};

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
    [[vk::location(3)]] SpriteInstance instance)
{
    // The shared primitive quad spans [-1, 1]; the authored quad is `size` world
    // units across, so half of the primitive range is the quad-space corner.
    float2 corner   = input.pos.xy * 0.5;
    float3 worldPos = instance.worldCenter + instance.axisX * corner.x + instance.axisY * corner.y;

    SpriteVertexOutput output;
    output.sv_position  = mul(uFrame.viewProj, float4(worldPos, 1.0));
    // uvRect is in image space (v0 = the image's top rows), while the shared
    // quad's texCoord.y grows toward +Y. The quad's top edge shows v0.
    float2 quadUv       = float2(input.texCoord.x, 1.0 - input.texCoord.y);
    output.texCoord     = lerp(instance.uvRect.xy, instance.uvRect.zw, quadUv);
    output.tint         = instance.tint;
    output.textureIndex = instance.textureIndex;
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
