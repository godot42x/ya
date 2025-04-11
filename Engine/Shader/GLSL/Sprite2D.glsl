#type vertex

#version 450 core

layout(location = 0) in vec3 aPos; 
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;

layout(set = 1, binding = 0) uniform CameraData {
    mat4 projection;
    mat4 view;
} uCamera;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

void main()
{
    gl_Position = uCamera.projection * uCamera.view * vec4(aPos, 1.0);
    fragColor = aColor;
    fragUV = aUV;
}

// =================================================================================================

#type fragment

#version 450 core

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;

layout(set = 2, binding = 0) uniform sampler2D uTexture0;

layout(location = 0) out vec4 outColor;

void main() 
{
    vec4 texColor = texture(uTexture0, fragUV);
    outColor = texColor * fragColor;
}

