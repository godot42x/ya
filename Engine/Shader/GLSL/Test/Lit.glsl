#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec3 aNormal;

layout(set =0, binding =0, std140) uniform FrameUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint frameIdx;
    float time;
} uFrame;


layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNormal;

layout(push_constant) uniform PushConstants{
    mat4 modelMat;
}pc;


void main (){
    gl_Position = uFrame.projMat * 
                  uFrame.viewMat * 
                  pc.modelMat * 
                  vec4(aPos, 1.0);
    vPos =  (pc.modelMat * vec4(aPos, 1.0)).xyz;
    vNormal  = aNormal;

}


#type fragment
#version 450

layout(set =0, binding =1, std140) uniform LightUBO {
    vec3 lightDir;
    float lightIntensity;
    vec3 lightColor;
    float ambientIntensity;
    vec3 ambientColor;
    vec3 PointLightPos;
} uLit;



layout(set = 1, binding = 0) uniform ParamUBO {
    vec3 objectColor;
} uParams;

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNormal;

layout(location = 0) out vec4 fColor;


void main (){
    vec3 norm = normalize(vNormal);
    vec3 pointLightDir = normalize(uLit.PointLightPos - vPos);
    float diff = max(dot(norm, pointLightDir), 0.0);
    vec3 diffuse = diff * uLit.lightColor;

    vec3 ambient = uLit.ambientIntensity * uLit.lightColor;

    vec3 objectColor = uParams.objectColor;

    vec3 color = (ambient +   diffuse) * objectColor;
    fColor = vec4(color, 1.0);
}