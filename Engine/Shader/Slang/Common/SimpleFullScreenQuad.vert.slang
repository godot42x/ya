#pragma once 

#include "Common/Helper.slang"

struct VertexOutput{
    vec4 clipPos: SV_Position;
    vec2 uv : UV;
};

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

    output.uv = pos * 0.5 + 0.5;
    output.clipPos = float4(pos, 0.0, 1.0);
}