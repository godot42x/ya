#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aTexCoord;
layout(location = 2) in vec4 aNormal;


layout(push_constant) uniform PushConstants {
    mat4 model;
} PC;


layout(location = 0) out vec4 fragColor;

void main()
{
    gl_Position = PC.model * vec4(aPos, 1.0);
    gl_PerVertex.fragColor = vec4(1.0, 0.0, 0.0, 1.0);
}

////////////////////////
#type fragment
#version 450

layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main()
{
    // Output the color directly
    outColor = fragColor;
}