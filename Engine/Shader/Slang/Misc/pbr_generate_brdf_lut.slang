#include "Common/Helper.slang"
#include "Misc/PBRCommon.slang"

// Precompute
// Irradiance convolution computing
// sample 1 face of a cubemap use kernel to get the irradiance map (辐照度)
// handle 1 face by 1 drawcall(off-screen rendering)


struct VertexInput{
    vec3 pos: POSITION;
};

struct VertexOutput{
    vec4 clipPos : SV_Position;
    vec2 uv: TEXCOORD0;
};


[shader("vertex")]
void vertMain(uint vertexId : SV_VertexID, out VertexOutput OUT)
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

    OUT.uv          = pos * 0.5 + 0.5;
    OUT.clipPos = float4(pos, 0.0, 1.0);
}


#ifndef SAMPLE_COUNT
    #define SAMPLE_COUNT 1024u
#endif



vec2 integrateBRDF(float NdotV, float roughness)
{
    vec3 V;
    V.x = sqrt(1.0 - NdotV * NdotV); // sin(theta)
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    vec3 N = vec3(0.0, 0.0, 1.0);
    for(uint i = 0u; i< SAMPLE_COUNT; ++i)
    {

        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(Xi, N, roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if(NdotL > 0.0)
        {
            float G = G_geometrySmith_IBL(N, V, L, roughness);
            float G_Vis = (G* VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return vec2(A,B);
}

[shader("fragment")]
vec4 fragMain(VertexOutput IN) : SV_Target
{
    vec2 integratedBRDF = integrateBRDF(IN.uv.x, IN.uv.y);
    return vec4(integratedBRDF, 0.0, 1.0);
}
