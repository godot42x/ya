#include "Common/Helper.slang"
#include "DeferredRender/Types.slang"


struct VertexInput{
    float3 pos : POSITION;
    float2 uv : UV;
};

struct VertexOutput{
    float4 sv_position : SV_Position;
    float2 uv : UV;
};



struct PushConstants
{
    float4x4 modelMat;
};
[[vk::push_constant]] PushConstants pc;



[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    // OUT.sv_position = uFrame.projMatrix * uFrame.viewMatrix * float4(IN.pos, 1.0);
    OUT.sv_position = float4(IN.pos, 1.0);
    OUT.uv = IN.uv;
}


struct FragmentOutput{
    float4 color: COLOR;
};



[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    vec3 pos = uTexGPos.Sample(IN.uv).rgb;
    vec3 normal = uTexGNormal.Sample(IN.uv).xyz;
    float4 albedoSpecular = uTexGAlbedoSpecular.Sample(IN.uv);

    vec3 viewDir =  normalize(uFrame.viewPos - pos);

    // Dir light
    vec3 lightVDir = normalize(-uLight.dirLight.dir);
    // vec3 viewDir = IN.sv_position - 
    vec3 dirLightColor = uLight.dirLight.color;

    // phong
    float diff = max(dot(lightVDir, normal), 0.0);
    vec3 halfwayDir = normalize(lightVDir+viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), uLight.dirLight.shininess);


    vec3 color = 0;

    vec3 albedo =  albedoSpecular.xyz;
    vec3 ambient =  albedo *  uLight.dirLight.ambient;
    vec3 diffuse =  diff *   albedo * uLight.dirLight.color;
    vec3 specular = albedoSpecular.w * spec * uLight.dirLight.color;
    // float shadow = calculateDirectionalShadow(IN.posInDirLightSpace, norm, lightDir);
    color +=  ambient + /*(1-shadow)  */ (diffuse + specular);

    OUT.color = vec4(color, 1.0);

}