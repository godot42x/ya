#type vertex

#version 450 core

layout(location = 0) in vec3 aPos; 
layout(location = 1) in vec4 aColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    gl_Position = vec4(aPos, 1.0);  // No transformation, direct position
    fragColor = aColor;
}

// =================================================================================================

#type fragment

#version 450 core

layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() 
{
    outColor = fragColor;  // Simple color output
}
