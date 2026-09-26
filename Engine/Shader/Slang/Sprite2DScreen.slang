// GUI compose shader (screen-space): the UIFrameSnapshot replay paths
// (chrome / game UI / designer canvas / offscreen mirror / tool surface).
// Owns the screen-space UI primitive set: textured sprites with
// Coverage/Sdf/Opaque sampling, SDF round-rect corners, per-vertex color.
// World-space billboards live in Sprite2DWorld.slang; debug lines in
// Sprite2DLine.slang.

#include "Common/Limits.slang"

#ifndef TEXTURE_SET_SIZE
#define TEXTURE_SET_SIZE 16
#endif

#ifndef SAMPLE_MODE_COVERAGE
#define SAMPLE_MODE_COVERAGE 0
#endif

#ifndef SAMPLE_MODE_SDF
#define SAMPLE_MODE_SDF 1
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
    // Typed draw data: the descriptor-array slot and the sampling mode are
    // separate fields (no bit packing). The mode is per quad -- the same
    // font atlas is sampled Coverage or Sdf depending on glyph size.
    [[vk::location(3)]] uint   textureSlot : TEXCOORD_TEXTURE_SLOT;
    [[vk::location(4)]] uint   sampleMode  : TEXCOORD_SAMPLE_MODE;
    [[vk::location(5)]] float3 corner      : TEXCOORD_CORNER;
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float4 color       : COLOR0;
    float2 texCoord    : TEXCOORD;
    nointerpolation uint  textureSlot : TEXCOORD_TEXTURE_SLOT;
    nointerpolation uint  sampleMode  : TEXCOORD_SAMPLE_MODE;
    // corner = (radiusPx, quadW, quadH); radiusPx > 0 routes the fragment into
    // the SDF round-rect alpha branch.
    nointerpolation float3 corner : TEXCOORD_CORNER;
};

float4 sampleTexturedSprite(VertexOutput input)
{
    // The draw carries its descriptor-array slot explicitly; slots are
    // recycled after texture-overflow flushes, so this must remain per draw
    // rather than live in a frame-level mask.
    const uint textureIdx = input.textureSlot;
    // SDF glyph atlas: the R channel carries the signed distance (128 =
    // boundary, FreeType FT_RENDER_MODE_SDF). Convert to coverage with
    // SCREEN-SPACE AA: the transition width is one screen pixel regardless of
    // scale (fwidth measures d's change per pixel), so text stays crisp at
    // any size.
    if (input.sampleMode == SAMPLE_MODE_SDF) {
        float d = uTextures[textureIdx].Sample(input.texCoord).r;
        // HALF-pixel AA: fwidth(d) is d's change per screen pixel; spanning
        // +-0.25*fwidth gives a 0.5px transition. A 1px stroke then keeps a
        // bright core (a full-pixel AA span would swallow the whole stroke
        // and render thin stems washed out — the "blurry letters" look).
        float alpha = smoothstep(0.5 - fwidth(d) * 0.25, 0.5 + fwidth(d) * 0.25, d);
        // Light contrast boost: SDF thin strokes read slightly lighter than
        // hinted bitmaps at small sizes; a mild power curve restores weight.
        alpha = pow(alpha, 0.72);
        return float4(input.color.rgb, input.color.a * alpha);
    }
    float4 texColor = uTextures[textureIdx].Sample(input.texCoord);
    // Viewport / scene RTs often leave alpha unused (0). Discard would punch
    // the whole sprite to the panel behind it. The opaque sample mode
    // requests opaque RGB.
    if (input.sampleMode == SAMPLE_MODE_OPAQUE) {
        texColor.a = 1.0;
    }
    else if (texColor.a < 0.01) {
        discard;
    }
    return texColor * input.color;
}

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.sv_position = mul(uFrame.viewProj, float4(input.pos, 1.0));
    output.color       = input.color;
    output.texCoord    = input.texCoord;
    output.textureSlot = input.textureSlot;
    output.sampleMode  = input.sampleMode;
    output.corner      = input.corner;
    return output;
}

// SDF round-rect alpha for a screen-space quad. corner = (radiusPx, quadW, quadH).
// Builds the local signed distance in px from uv in [0,1], then AA's the
// boundary with fwidth so the rounded corners stay crisp at any resolution.
float roundedRectAlpha(float3 corner, float2 uv)
{
    // local position in px, origin at quad center.
    float2 p = (uv - 0.5) * corner.yz;
    // half-size minus the corner radius.
    float2 half = corner.yz * 0.5 - corner.x;
    float2 q = abs(p) - half;
    float dist = length(max(q, float2(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - corner.x;
    // AA with screen-space derivative (px per fragment).
    float aa = fwidth(dist) * 0.75 + 1e-4;
    return 1.0 - smoothstep(-aa, aa, dist);
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    if (input.corner.x > 0.0) {
        float4 src = sampleTexturedSprite(input);
        float a = roundedRectAlpha(input.corner, input.texCoord);
        return float4(src.rgb, src.a * a);
    }
    return sampleTexturedSprite(input);
}
