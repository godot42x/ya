#include "Common/Helper.slang"
#include "DeferredRender/Types.slang"


struct VertexInput{
    float3 pos : POSITION;
    float2 uv : UV;
};

struct VertexOutput{
    float3 sv_position : SV_Position;
    vec3  pos: POSITION; 
    float3 normal : NORMAL;
    float4 albedoSpecular: ALBEDO_SPECULAR;
};


struct PushConstants
{
    float4x4 modelMat;
};

struct FrameData{
    float3 viewPos;
    mat4  viewMatrix;
    mat4  projMatrix;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0,0)]] ConstantBuffer<FrameData,Std140DataLayout> uFrame;
[[vk::binding(1,0)]] ConstantBuffer<LightData,Std140DataLayout> uLight;

[[vk::binding(0,1)]] Sampler2D uTexGPos;
[[vk::binding(1,1)]] Sampler2D uTexGNormal;
[[vk::binding(2,1)]] Sampler2D uTexGAlbedoSpecular;


[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    vec3 pos = uTexGPos.Sample(IN.uv).rgb;
    OUT.sv_position = pos;
    vec4 viewSpacePos = uFrame.projMatrix * uFrame.viewMatrix * vec4(pos, 1.0);
    OUT.pos =  viewSpacePos.xyz;

    OUT.normal = uTexGNormal.Sample(IN.uv).xyz;
    OUT.albedoSpecular = uTexGAlbedoSpecular.Sample(IN.uv);
}


struct FragmentOutput{
    float4 color: COLOR;
};

struct DirectionalLight{
    vec3 dir;
    vec3 color;
    float ambient;
    float shininess;
};

struct LightData{
    DirectionalLight dirLight;
};

struct ParamsData{

};



[shader("fragment")]
void fragMain(in VertexOutput IN, out FragmentOutput OUT)
{
    vec3 viewDir =  normalize(uFrame.viewPos - IN.sv_position);

    // Dir light
    vec3 lightVDir = normalize(-uLight.dirLight.dir);
    // vec3 viewDir = IN.sv_position - 
    vec3 dirLightColor = uLight.dirLight.color;

    // phong
    float diff = max(dot(lightVDir, IN.normal), 0.0);
    vec3 halfwayDir = normalize(lightVDir+viewDir);
    float spec = pow(max(dot(IN.normal, halfwayDir), 0.0), uLight.dirLight.shininess);


    vec3 color = 0;

    vec3 albedo =  IN.albedoSpecular.xyz;
    vec3 ambient =  albedo *  uLight.dirLight.ambient;
    vec3 diffuse =  diff *   albedo * uLight.dirLight.color;
    vec3 specular = IN.albedoSpecular.w * spec * uLight.dirLight.color;
    // float shadow = calculateDirectionalShadow(IN.posInDirLightSpace, norm, lightDir);
    color +=  ambient + /*(1-shadow)  */ (diffuse + specular);

    OUT.color = vec4(color, 1.0);

}