#type vertex
#version 450

layout(location = 0) in vec3 aPos;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
    mat4 view;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} pc;

void main()
{
    gl_Position = ubo.projection * ubo.view * pc.model * vec4(aPos, 1.0);
}

#type fragment
#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = pc.color;
}
