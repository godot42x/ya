#include "Common/Limits.slang"

#ifndef TEXTURE_SET_SIZE
#define TEXTURE_SET_SIZE 16
#endif

static const uint kTextureIndexMask = 0x3FFFFFFFu;
static const uint kTextureModeShift = 30u;
static const uint kSampleModeSdf = 1u;
static const uint kSampleModeOpaque = 2u;

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
    [[vk::location(3)]] uint   textureRef  : TEXCOORD_TEXTURE_REF;
    [[vk::location(4)]] float3 worldCenter : TEXCOORD_WORLD_CENTER;
    [[vk::location(5)]] float3 worldDirection : TEXCOORD_WORLD_DIR;
    [[vk::location(6)]] float2 worldSize   : TEXCOORD_WORLD_SIZE;
    [[vk::location(7)]] float3 corner     : TEXCOORD_CORNER;
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float4 color       : COLOR0;
    float2 texCoord    : TEXCOORD;
    nointerpolation uint textureRef : TEXCOORD_TEXTURE_REF;
    // corner = (radiusPx, quadW, quadH); radiusPx > 0 routes the fragment into
    // the SDF round-rect alpha branch (screen quads only).
    nointerpolation float3 corner : TEXCOORD_CORNER;
};

float4 sampleTexturedSprite(VertexOutput input)
{
    // The draw carries its descriptor-array slot and sampling mode together.
    // Slots are recycled after texture-overflow flushes, so this metadata must
    // remain per draw rather than live in a frame-level mask.
    const uint textureRef = input.textureRef;
    const uint textureIdx = textureRef & kTextureIndexMask;
    const uint sampleMode = textureRef >> kTextureModeShift;
    // SDF glyph atlas (high bit set): the R channel carries the signed
    // distance (128 = boundary, FreeType FT_RENDER_MODE_SDF). Convert to
    // coverage with SCREEN-SPACE AA: the transition width is one screen
    // pixel regardless of scale (fwidth measures d's change per pixel), so
    // text stays crisp at any size.
    if (sampleMode == kSampleModeSdf) {
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
    // the whole sprite to the panel behind it. Bit 30 requests opaque RGB.
    if (sampleMode == kSampleModeOpaque) {
        texColor.a = 1.0;
    }
    else if (texColor.a < 0.01) {
        discard;
    }
    return texColor * input.color;
}

float2 flipWorldUv(float2 uv)
{
    return float2(uv.x, 1.0 - uv.y);
}

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.sv_position = mul(uFrame.viewProj, float4(input.pos, 1.0));
    output.color       = input.color;
    output.texCoord    = input.texCoord;
    output.textureRef  = input.textureRef;
    output.corner      = input.corner;
    return output;
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
    output.textureRef  = input.textureRef;
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
