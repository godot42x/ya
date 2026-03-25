#include "Common/Helper.slang"
#include "DeferredRender/Types.slang"


struct VertexInput{
    [[vk::location(0)]] float3 pos      : POSITION;
    [[vk::location(1)]] float2 uv       : TEXCOORD0;
    [[vk::location(2)]] float3 normal   : NORMAL;
    [[vk::location(3)]] float3 tangent  : TANGENT;
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

    OUT.sv_position = clipPos;
    OUT.pos = worldPos.xyz;
    OUT.uv = IN.uv;

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
    float4 pos : SV_Target0;
    float4 normal : SV_Target1;
    float4 albedoSpecular : SV_Target2;
};




[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    OUT.pos = vec4(IN.pos, 1.0);
    OUT.normal = vec4(normalize(IN.normal), 1.0);

    float3 albedo = uTexDiffuse.Sample(IN.uv).rgb;
    float specular = uTexSpecular.Sample(IN.uv).r;
    OUT.albedoSpecular = float4(albedo, specular);
}