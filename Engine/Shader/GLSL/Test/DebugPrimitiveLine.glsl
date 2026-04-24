#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
    mat4 view;
} ubo;

layout(location = 0) out vec4 vColor;

void main()
{
    gl_Position = ubo.projection * ubo.view * vec4(aPos, 1.0);
    vColor      = aColor;
}

#type fragment
#version 450

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor;
}
