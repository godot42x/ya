#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;


layout(push_constant) uniform PushConstants {
    mat4 projection;  
    mat4 view;  
    mat4 model;          // 基础Model矩阵（用于旋转等）
    uint colorType;
} pc;


layout(location = 0) out vec4 vColor;

void main()
{
    mat4 instanceModel = pc.model;
    vec4 worldPos = instanceModel * vec4(aPos, 1.0);
    // vec3 normal = normalize(aNormal.xyz);

    gl_Position = pc.projection * pc.view * worldPos;
    switch(pc.colorType){
    case 0:{
        vColor = vec4(aNormal * 0.5 + 0.5, 10.0);
    }break;
    case 1:{
        vColor = vec4(aTexCoord, 0.0, 1.0);
    }break;;
    case 2:{
        // purple
        vColor = vec4(0.0, 0.8, 0.8, 1.0);
        vColor = vec4(aNormal * 0.5 + 0.5, 10.0);
    }break;
    }
}

#type fragment
#version 450

layout(location = 0) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    // Output the color directly
    outColor = vColor;
}