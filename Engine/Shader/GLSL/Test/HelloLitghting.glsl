#type vertex
#version 450

layout(location = 0) in vec3 aPos;

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
    gl_Position = uFrame.projMat * uFrame.viewMat * pc.modelMat * vec4(aPos, 1.0);
}


#type fragment
#version 450

layout(set =0, binding =1, std140) uniform LitUBO {
    vec3 lightDir;
    vec3 lightColor;
    float lightIntensity;
    vec3 ambientColor;
    float ambientIntensity;
} uLit;


layout(set = 1, binding = 0) uniform ObjectUBO{
    vec3 objectColor;
}uMaterial;



layout(location = 0) out vec4 fColor;


void main (){
    vec3 color = uLit.ambientColor * uLit.ambientIntensity + uLit.lightColor * uLit.lightIntensity;
    fColor = vec4(color, 1.0);
}