#include "Common/Helper.slang"
#include "Misc/PBRCommon.slang"
struct PushConstants{
    mat4 view;
    mat4 projection;
    float roughness;
};

[[vk::push_constant]] PushConstants pc;

struct VertexInput{
    vec3 pos: POSITION;
};

struct VertexOutput{
    vec4 clipPos : SV_Position;
    vec3 localPos: TEXCOORD0;
};


[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    OUT.clipPos = mul(pc.projection, mul(pc.view, vec4(IN.pos, 1.0)));
    OUT.localPos = IN.pos;
}

[[vk::binding(0,0)]] SamplerCube environmentMap;

#ifndef SAMPLE_COUNT
    #define SAMPLE_COUNT 1024u
#endif

[shader("fragment")]
vec4 fragMain(VertexOutput IN) : SV_Target
{
    vec3 N = normalize(IN.localPos);
    vec3 R  =N;
    vec3 V = R;

    float totalWeight = 0.0;
    vec3 preFilteredColor = vec3(0.0);

    for(uint i =0; i< SAMPLE_COUNT; ++i)
    {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(Xi, N, pc.roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H -V);

        float NdotL = max(dot(N,L), 0.0); // angle between normal and light direction
        if(NdotL > 0.0)
        {
            preFilteredColor +=  environmentMap.Sample(L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    preFilteredColor = preFilteredColor / totalWeight;

    return vec4(preFilteredColor, 1.0);
}