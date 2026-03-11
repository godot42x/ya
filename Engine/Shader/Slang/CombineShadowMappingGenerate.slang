// @gen-skip: slangc crashes on this file (maxvertexcount macro arithmetic, see slang#10506)
#define MAX_POINT_LIGHTS 4

struct PushConstants
{
    float4x4 modelMat;
};

[[vk::push_constant]] PushConstants pc;

enum ECubeFace
{
    CubeFace_PosX = 0,
    CubeFace_NegX = 1,
    CubeFace_PosY = 2,
    CubeFace_NegY = 3,
    CubeFace_PosZ = 4,
    CubeFace_NegZ = 5,
    CubeFace_Count
};

struct FrameData
{
    float4x4 directionalLightMatrix;
    float4x4 pointLightMatrices[6 * MAX_POINT_LIGHTS]; // 6 cube faces per light
    uint     numPointLights;
    uint     hasDirectionalLight;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> uFrame;

struct VSInput
{
    float3 position : POSITION;
};

[shader("vertex")]
float4 vertMain(VSInput input) : SV_Position
{
    return mul(pc.modelMat, float4(input.position, 1.0));
}

struct GSInput
{
    float4 position : SV_Position;
};

struct GSOutput
{
    float4 position : SV_Position;
    uint   layer    : SV_RenderTargetArrayIndex;
};

// #define MAX_VERTICES_COUNT ((MAX_POINT_LIGHTS * 6 +1)*3)
[shader("geometry")]
[maxvertexcount(75)] // (MAX_POINT_LIGHTS=4) * 6 faces + 1 directional) * 3 vertices
// [maxvertexcount( (MAX_POINT_LIGHTS * 6 +1)*3) ] 
void geomMain(triangle GSInput input[3], inout TriangleStream<GSOutput> outStream)
{
    // directional light — layer 0
    if (uFrame.hasDirectionalLight == 1)
    {
        for (int vertIdx = 0; vertIdx < 3; ++vertIdx)
        {
            GSOutput o;
            o.layer    = 0;
            o.position = mul(uFrame.directionalLightMatrix, input[vertIdx].position);
            outStream.Append(o);
        }
        outStream.RestartStrip();
    }

    // point lights — pos_x/neg_x/pos_y/neg_y/pos_z/neg_z per light, layers 1+
    for (uint lightIdx = 0; lightIdx < uFrame.numPointLights; ++lightIdx)
    {
        for (uint faceIdx = 0; faceIdx < 6; ++faceIdx)
        {
            uint index = lightIdx * 6 + faceIdx;
            for (uint vertIdx = 0; vertIdx < 3; ++vertIdx)
            {
                GSOutput o;
                o.layer    = 1 + index;
                o.position = mul(uFrame.pointLightMatrices[index], input[vertIdx].position);
                outStream.Append(o);
            }
            outStream.RestartStrip();
        }
    }
}

[shader("fragment")]
void fragMain()
{
    // depth is written automatically by the driver
}