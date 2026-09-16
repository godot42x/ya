// Debug wireframe shapes (spheres, cylinders, cones) drawn from the debug
// primitive pool.
//
// Ported from Engine/Shader/GLSL/Test/DebugPrimitiveShape.glsl: the mesh is a
// unit primitive, the instance transform and flat colour arrive as push
// constants, and the fragment stage simply emits that colour.

struct FrameUBO
{
    float4x4 projection;
    float4x4 view;
};

struct PushConstants
{
    float4x4 model;
    float4   color;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameUBO, Std140DataLayout> uFrame;
[[vk::push_constant]] PushConstants pc;

struct VertexInput
{
    [[vk::location(0)]] float3 pos : POSITION;
};

[shader("vertex")]
float4 vertMain(VertexInput input) : SV_Position
{
    return mul(uFrame.projection, mul(uFrame.view, mul(pc.model, float4(input.pos, 1.0))));
}

[shader("fragment")]
float4 fragMain() : SV_Target
{
    return pc.color;
}
