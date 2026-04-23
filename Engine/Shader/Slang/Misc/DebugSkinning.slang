#include "DeferredRender/Types.slang"

/**
this pass maybe be merge to origin shader, because calculate the pos will cost more performance, even if in editor?
*/

struct VertexInput
{
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(4)]] vector<int32_t, MAX_BONE_WEIGHT_PER_VERTEX> boneIDs : BONE_IDS;
    [[vk::location(5)]] vector<float, MAX_BONE_WEIGHT_PER_VERTEX> weights : WEIGHTS;
};

struct VertexOutput
{
    float4 clipPos : SV_Position;
    [[vk::location(0)]] vector<int32_t, MAX_BONE_WEIGHT_PER_VERTEX> boneIDs : BONE_IDS;
    [[vk::location(1)]] vector<float, MAX_BONE_WEIGHT_PER_VERTEX> weights : WEIGHTS;
};

struct PushConstants
{
    float4x4 projection;
    float4x4 view;
    float4x4 model;
    int32_t  pickingBone;
};

[[vk::push_constant]] PushConstants pc;

[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    float4 worldPos = mul(pc.model, float4(IN.pos, 1.0));
    OUT.clipPos     = mul(pc.projection, mul(pc.view, worldPos));
    OUT.boneIDs     = IN.boneIDs;
    OUT.weights     = IN.weights;
}

[shader("fragment")]
void fragMain(in VertexOutput IN, out float4 color : SV_Target)
{
    color = float4(0.0, 0.0, 0.0, 0.0);

    for (uint32_t index = 0; index < MAX_BONE_WEIGHT_PER_VERTEX; ++index)
    {
        if (IN.boneIDs[index] != pc.pickingBone) {
            continue;
        }

        float weight = saturate(IN.weights[index]);
        if (weight >= 0.7) {
            color = RED * weight;
        }
        else if (weight >= 0.4) {
            color = GREEN * weight;
        }
        else {
            color = BLUE * weight;
        }
        return;
    }
}
