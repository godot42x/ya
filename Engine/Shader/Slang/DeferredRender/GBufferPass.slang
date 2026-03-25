#include "Common/Helper.slang"
#include "DeferredRender/Types.slang"


struct VertexInput{
    float3 pos      :Position;
    float3 normal : Normal;
    float2 uv       :UV;
    float3 tangent :Tangent;
};

struct VertexOutput{
    float4 sv_position: SV_Position;
    float3 pos :Position;
    float2 uv   :UV;
    float3 normal :Normal;
    float3x3 TBN;
};


struct PushConstants
{
    float4x4 modelMat;
};

[[vk::push_constant]] PushConstants pc;



[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    float4 worldPos = pc.modelMat * float4(IN.pos, 1.0);
    float4 clipPos = uFrame.projMatrix * uFrame.viewMatrix * worldPos;

    OUT.pos = worldPos.xyz;
    OUT.uv = IN.uv;
    OUT.sv_position = clipPos;

    // float3 T = normalize( (pc.modelMat * float4(IN.tangent, 0.0)).xyz);
    // float3 N = normalize( (pc.modelMat * float4(IN.normal, 0.0)).xyz);
    // float3 B = cross(N,T);
    // float3x3 TBN = float3x3(T, B, N);
    // TBN =  transpose(TBN); // now TBN is the TBN^-1
    // OUT.TBN = TBN;

    // TODO: support normal map texture, by calculate TBN then pass to fragments
    float3x3 normalMat = transpose(inverse3x3(float3x3(pc.modelMat)));
    OUT.normal =  mul(normalMat, IN.normal);
}


// MARK: ========

struct FragmentOutput{
    float3 pos : Position;
    float3 normal : Normal;
    float4 albedoSpecular : AlbedoSpecular;

};




[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    OUT.pos = IN.pos;

    // normal
    // vec2 uv = (IN.TBN *  vec3(IN.uv, 0)).xy;
    // OUT.normal =  normalize(uTexNormal.Sample(uv).rgb * 2.0 - 1.0); // normal map is in [0,1], need to remap to [-1,1]
    OUT.normal = normalize(IN.normal);

    float3 albedo = uTexDiffuse.Sample(IN.uv).rgb;
    float specular = uTexSpecular.Sample(IN.uv).r; // TODO: no specular texture use 1.0 by default?
    OUT.albedoSpecular = float4(albedo, specular);
}