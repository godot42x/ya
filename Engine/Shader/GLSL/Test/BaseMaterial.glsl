#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;


layout(push_constant) uniform PushConstants {
    mat4 viewProjection;  // VP矩阵，不包含Model
    mat4 model;          // 基础Model矩阵（用于旋转等）
    uint colorType;
} pc;


layout(location = 0) out vec4 vColor;

void main()
{
    mat4 instanceModel = pc.model;
    vec4 worldPos = instanceModel * vec4(aPos, 1.0);
    // vec3 normal = normalize(aNormal.xyz);

    gl_Position = pc.viewProjection * worldPos;
    vColor = pc.colorType == 0? vec4(aNormal * 0.5 + 0.5, 1.0): vec4(aTexCoord, 0.0, 1.0);
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