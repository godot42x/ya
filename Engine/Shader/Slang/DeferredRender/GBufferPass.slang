#include "Common/Limits.slang"
#include "Common/Helper.slang"


struct VertexInput{
    float3 pos      :Position;
    float3 normal : Normal;
    float2 uv       :UV;
    float3 tangent :Tangent;
};

struct VertexOutput{
    float3 pos :Position;
    float2 uv   :UV;
    float3 normal :Normal;
    // mat3 TBN;
};


struct PushConstants
{
    float4x4 modelMat;
};

[[vk::push_constant]] PushConstants pc;

[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    float4 pos = mul( pc.modelMat , float4(IN.pos,1.0));
    OUT.pos = pos.xyz;
    OUT.uv = IN.uv;

    // vec3 T = normalize(pc.modelMat * vec4(aTangent, 0.0));
    // vec3 N = normalize (pc.modelMat * vec4(aNormal, 0.0));
    // vec3 B = cross(N,T);
    // mat3 TBN = mat3(T, B, N);
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


[[vk::binding(0, 1)]]  Sampler2D  uTexDiffuse;
[[vk::binding(1, 1)]]  Sampler2D  uTexSpecular;


[shader("fragment")]
void fragMain(in VertexInput IN, out FragmentOutput OUT)
{
    OUT.pos = IN.pos;
    OUT.normal = normalize(IN.normal);
    float3 albedo = uTexDiffuse.Sample(IN.uv).rgb;
    float specular = uTexSpecular.Sample(IN.uv).r;
    OUT.albedoSpecular = float4(albedo, specular);
}