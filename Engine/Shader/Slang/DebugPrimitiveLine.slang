// Screen-space debug lines (physics/gizmo helpers) drawn as a line list.
//
// Ported from Engine/Shader/GLSL/Test/DebugPrimitiveLine.glsl; the vertices are
// already transformed to world space by the writer, so vertex and fragment only
// apply view-projection and pass the per-vertex colour through.

struct FrameUBO
{
    float4x4 projection;
    float4x4 view;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameUBO, Std140DataLayout> uFrame;

struct VertexInput
{
    [[vk::location(0)]] float3 pos   : POSITION;
    [[vk::location(1)]] float4 color : COLOR;
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.sv_position = mul(uFrame.projection, mul(uFrame.view, float4(input.pos, 1.0)));
    output.color       = input.color;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    return input.color;
}
