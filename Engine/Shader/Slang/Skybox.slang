// Skybox background cube.
//
// Ported from Engine/Shader/GLSL/Skybox.glsl. Bindings and behaviour are kept
// identical: set 0 binding 0 carries the frame matrices, set 1 binding 0 the
// cube map, and the cube pushes itself onto the far plane (z = w) so it never
// occludes scene geometry while depth writes stay disabled.

struct FrameUBO
{
    float4x4 proj;
    float4x4 view;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameUBO, Std140DataLayout> uFrame;
[[vk::binding(0, 1)]] SamplerCube                               uSkyBox;

struct VertexInput
{
    [[vk::location(0)]] float3 pos : POSITION;
};

struct VertexOutput
{
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float3 uv : UV;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.uv            = input.pos;
    output.sv_position   = mul(uFrame.proj, mul(uFrame.view, float4(input.pos, 1.0)));
    output.sv_position.z = output.sv_position.w;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    return uSkyBox.Sample(input.uv);
}
