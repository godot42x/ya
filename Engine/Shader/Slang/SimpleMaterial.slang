// Simple material used by the overlay/debug draw paths (Deferred overlay stage
// and the Forward aux passes) to visualise normals and UVs of arbitrary meshes.
//
// Ported from Engine/Shader/GLSL/Test/SimpleMaterial.glsl: the whole transform
// chain arrives through push constants (the C++ side fills projection/view/model
// from the viewport), and colorType selects which vertex channel is displayed.

struct PushConstants
{
    float4x4 projection;
    float4x4 view;
    float4x4 model;
    uint     colorType;
};

[[vk::push_constant]] PushConstants pc;

struct VertexInput
{
    [[vk::location(0)]] float3 pos      : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD_UV;
    [[vk::location(2)]] float3 normal   : NORMAL;
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
    output.sv_position = mul(pc.projection, mul(pc.view, mul(pc.model, float4(input.pos, 1.0))));

    switch (pc.colorType)
    {
    case 0:
        output.color = float4(input.normal * 0.5 + 0.5, 10.0);
        break;
    case 1:
        output.color = float4(input.texCoord, 0.0, 1.0);
        break;
    case 2:
        // SimpleMaterial::EColor::Default - reused by the debug normal view.
        output.color = float4(input.normal * 0.5 + 0.5, 10.0);
        break;
    default:
        // Not reachable through SimpleMaterial::EColor, but the shader must
        // hand a defined value to the rasterizer for any 32-bit push constant.
        output.color = float4(1.0, 1.0, 1.0, 1.0);
        break;
    }
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    return input.color;
}
