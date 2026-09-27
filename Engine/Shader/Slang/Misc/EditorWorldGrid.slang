// Infinite editor ground grid on the XZ plane (y = 0).
// A fullscreen triangle; each fragment intersects the camera ray with the plane.
// Minor lines every 1 unit, major lines every 10, X axis red, Z axis blue.
// Only line fragments survive, so the gaps do not write depth.

struct VertexOutput
{
    float4 clipPos : SV_Position;
    float2 ndc     : NDC;
};

struct PushConstants
{
    float4x4 invViewProjection;
    float4   clipZRow;
    float4   clipWRow;
    float4   cameraPos; // xyz = world camera, w = fade end distance
};

[[vk::push_constant]]
PushConstants pc;

[shader("vertex")]
void vertMain(uint vertexId : SV_VertexID, out VertexOutput output)
{
    float2 pos;
    if (vertexId == 0) {
        pos = float2(-1.0, -1.0);
    }
    else if (vertexId == 1) {
        pos = float2(3.0, -1.0);
    }
    else {
        pos = float2(-1.0, 3.0);
    }

    output.ndc     = pos;
    output.clipPos = float4(pos, 0.0, 1.0);
}

struct FragmentOutput
{
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

float axisLine(float coord, float thicknessPx)
{
    float fw     = max(fwidth(coord), 1e-5);
    float distPx = abs(coord) / fw;
    float halfW  = thicknessPx * 0.5;
    return 1.0 - smoothstep(halfW - 0.5, halfW + 0.5, distPx);
}

float gridLine(float2 coord, float thicknessPx)
{
    float2 fw     = max(fwidth(coord), float2(1e-5, 1e-5));
    float2 distPx = abs(frac(coord - 0.5) - 0.5) / fw;
    float  dist   = min(distPx.x, distPx.y);
    float  halfW  = thicknessPx * 0.5;
    return 1.0 - smoothstep(halfW - 0.5, halfW + 0.5, dist);
}

float lodFade(float2 coord)
{
    float2 fw = fwidth(coord);
    return 1.0 - saturate((max(fw.x, fw.y) - 0.35) / 1.0);
}

[shader("fragment")]
FragmentOutput fragMain(VertexOutput input)
{
    float4 nearH = mul(pc.invViewProjection, float4(input.ndc, 0.0, 1.0));
    float4 farH  = mul(pc.invViewProjection, float4(input.ndc, 1.0, 1.0));
    float3 nearW = nearH.xyz / nearH.w;
    float3 farW  = farH.xyz / farH.w;
    float3 ray   = farW - nearW;

    FragmentOutput output;
    output.color = float4(0.0, 0.0, 0.0, 0.0);
    output.depth = 1.0;
    if (abs(ray.y) < 1e-6) {
        discard;
        return output;
    }

    float t = -nearW.y / ray.y;
    if (t < 0.0) {
        discard;
        return output;
    }

    float3 hit   = nearW + ray * t;
    float4 hitH  = float4(hit, 1.0);
    float  clipW = dot(pc.clipWRow, hitH);
    float  clipZ = dot(pc.clipZRow, hitH);
    if (clipW <= 0.0) {
        discard;
        return output;
    }

    float depth = clipZ / clipW;
    if (depth < 0.0 || depth > 1.0) {
        discard;
        return output;
    }

    float2 xz = hit.xz;

    float minor = gridLine(xz, 1.0) * lodFade(xz);
    float major = gridLine(xz / 10.0, 2.25) * lodFade(xz / 10.0);
    float axisX = axisLine(xz.y, 2.75);
    float axisZ = axisLine(xz.x, 2.75);

    float3 minorColor = float3(0.32, 0.35, 0.40);
    float3 majorColor = float3(0.62, 0.66, 0.72);
    float3 axisXColor = float3(0.86, 0.22, 0.18);
    float3 axisZColor = float3(0.20, 0.42, 0.92);

    float3 color = minorColor;
    float  alpha = minor * 0.55;
    color = lerp(color, majorColor, saturate(major));
    alpha = max(alpha, major * 0.9);
    color = lerp(color, axisZColor, saturate(axisZ));
    alpha = max(alpha, axisZ);
    color = lerp(color, axisXColor, saturate(axisX));
    alpha = max(alpha, axisX);

    float dist     = length(hit - pc.cameraPos.xyz);
    float fadeEnd  = max(pc.cameraPos.w, 1.0);
    float distFade = 1.0 - smoothstep(fadeEnd * 0.35, fadeEnd, dist);
    float grazing  = saturate(abs(normalize(ray).y) * 5.0);
    alpha *= distFade * grazing;

    if (alpha < 0.02) {
        discard;
        return output;
    }

    // Constant nudge in D32. A slope bias from fwidth blows up at the horizon
    // and lets the grid draw in front of distant geometry.
    depth = max(depth - 1e-5, 0.0);

    output.color = float4(color, alpha);
    output.depth = depth;
    return output;
}
