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



layout(push_constant) uniform PushConstants{
    mat4 modelMat;
}pc;


void main (){
    gl_Position = uFrame.projMat * 
                  uFrame.viewMat * 
                  pc.modelMat * 
                  vec4(aPos, 1.0);
}


#type fragment
#version 450

layout(set =0, binding =1, std140) uniform LightUBO {
    vec3 lightDir;
    vec3 lightColor;
    float lightIntensity;
    vec3 ambientColor;
    float ambientIntensity;
} uLit;


layout(set = 1, binding = 0) uniform ParamUBO {
    vec3 objectColor;
} uParams;


layout(location = 0) out vec4 fColor;


void main (){
    // vec3 color = uLit.ambientColor * uLit.ambientIntensity * uParams.objectColor +
    //              uLit.lightColor * uLit.lightIntensity * max(dot(normalize(vec3(0,0,1)), -uLit.lightDir), 0.0) * uParams.objectColor;
    vec3 color = uLit.lightColor * uParams.objectColor;
    fColor = vec4(color, 1.0);
}