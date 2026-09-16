// Debug viewport shading: normal colour, normal direction gizmo, depth and UV.
//
// Ported from Engine/Shader/GLSL/Test/DebugRender.glsl. The geometry stage is
// optional and exists for one mode only: with DEBUG_NORMAL_DIR defined it turns
// each triangle into the three world axes plus the face normal (line_strip),
// otherwise it is a triangle_strip pass-through. The C++ side only builds that
// variant when the device reports geometry shader support
// (IRender::supportsGeometryShader).

#include "Common/Helper.slang"

struct DebugUBO
{
    float4x4 projMat;
    float4x4 viewMat;
    int2     resolution;
    int      mode;
    float    time;
    float4   floatParam;
};

struct PushConstants
{
    float4x4 modelMat;
};

[[vk::binding(0, 0)]] ConstantBuffer<DebugUBO, Std140DataLayout> uDebug;
[[vk::push_constant]] PushConstants pc;

struct VertexInput
{
    [[vk::location(0)]] float3 pos      : POSITION;
    [[vk::location(1)]] float2 texcoord : TEXCOORD_UV;
    [[vk::location(2)]] float3 normal   : NORMAL;
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float2 texcoord    : TEXCOORD_UV;
    [[vk::location(1)]] float3 worldNormal : NORMAL;
    [[vk::location(2)]] float3 worldPos    : WORLD_POS;
};

struct GeometryOutput
{
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float2 texcoord    : TEXCOORD_UV;
    [[vk::location(1)]] float3 worldNormal : NORMAL;
    [[vk::location(2)]] float4 color       : COLOR;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    float4       pos   = mul(pc.modelMat, float4(input.pos, 1.0));

    output.worldPos    = pos.xyz;
    output.sv_position = mul(uDebug.projMat, mul(uDebug.viewMat, pos));

    float3x3 normalMatrix = transpose(inverse3x3(float3x3(pc.modelMat)));
    output.worldNormal    = normalize(mul(normalMatrix, input.normal));
    output.texcoord    = input.texcoord;
    return output;
}

#ifdef DEBUG_NORMAL_DIR

inline void emitAxisLine(inout LineStream<GeometryOutput> stream, float4 start, float4 end, float4 color)
{
    GeometryOutput vertex;
    vertex.texcoord    = float2(0.0, 0.0);
    vertex.worldNormal = float3(0.0, 0.0, 0.0);
    vertex.color       = color;

    vertex.sv_position = start;
    stream.Append(vertex);
    vertex.sv_position = end;
    stream.Append(vertex);
    stream.RestartStrip();
}

[maxvertexcount(12)]
[shader("geometry")]
void geomMain(triangle VertexOutput input[3], inout LineStream<GeometryOutput> stream)
{
    const float  axisLength = 0.4;
    const float4 axisX      = float4(1.0, 0.0, 0.0, 1.0);
    const float4 axisY      = float4(0.0, 1.0, 0.0, 1.0);
    const float4 axisZ      = float4(0.0, 0.0, 1.0, 1.0);
    const float4 normalTint = float4(0.8, 0.5, 0.02, 0.6);

    const float4x4 viewProj = mul(uDebug.projMat, uDebug.viewMat);

    // The gizmo is anchored at the triangle centroid so it does not clutter the
    // vertices of every face.
    const float4 center = float4((input[0].worldPos + input[1].worldPos + input[2].worldPos) / 3.0, 1.0);

    emitAxisLine(stream, mul(viewProj, center), mul(viewProj, center + axisX * axisLength), float4(1.0, 0.0, 0.0, 1.0));
    emitAxisLine(stream, mul(viewProj, center), mul(viewProj, center + axisY * axisLength), float4(0.0, 1.0, 0.0, 1.0));
    emitAxisLine(stream, mul(viewProj, center), mul(viewProj, center + axisZ * axisLength), float4(0.0, 0.0, 1.0, 1.0));
    // Offsetting before projecting is what makes this read as "the face
    // normal"; the GLSL original scaled the projected clip position by accident,
    // which aimed the line at the screen centre instead.
    emitAxisLine(stream,
                 mul(viewProj, center),
                 mul(viewProj, center + float4(input[0].worldNormal, 0.0) * (axisLength * 2.0)),
                 normalTint);
}

#else

[maxvertexcount(3)]
[shader("geometry")]
void geomMain(triangle VertexOutput input[3], inout TriangleStream<GeometryOutput> stream)
{
    for (int i = 0; i < 3; ++i)
    {
        GeometryOutput vertex;
        vertex.sv_position = input[i].sv_position;
        vertex.texcoord    = input[i].texcoord;
        vertex.worldNormal = input[i].worldNormal;
        vertex.color       = float4(0.0, 0.0, 0.0, 0.0);
        stream.Append(vertex);
    }
    stream.RestartStrip();
}

#endif

[shader("fragment")]
float4 fragMain(GeometryOutput input) : SV_Target
{
    // 1: shade the interpolated world normal
    if (uDebug.mode == 1)
    {
        return float4(normalize(input.worldNormal) * 0.5 + 0.5, 1.0);
    }

    // 2: whatever the geometry stage emitted (normal direction gizmo)
    if (uDebug.mode == 2)
    {
        return input.color;
    }

    // 3: linearise the rasterised depth for a fixed near/far range
    if (uDebug.mode == 3)
    {
        const float nearZ = 0.1;
        const float farZ  = 100.0;
        float       d     = input.sv_position.z;
        d                 = d * 2.0 - 1.0;
        d                 = (2.0 * nearZ * farZ) / ((farZ + nearZ) - d * (farZ - nearZ));
        d                 = (d - nearZ) / (farZ - nearZ);
        return float4(d, d, d, 1.0);
    }

    // 4: UV visualisation
    if (uDebug.mode == 4)
    {
        return float4(input.texcoord.x, input.texcoord.y, 0.0, 1.0);
    }

    discard;
    return float4(0.0, 0.0, 0.0, 0.0);
}
