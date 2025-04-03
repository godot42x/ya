#type vertex

#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aUV; // aka aTexCoord

// Camera/transform uniforms
layout(set = 1, binding = 0) uniform CameraBuffer {
    mat4 viewProjection;
} uCamera;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

void main()
{
    // Transform vertex position with view-projection matrix
    gl_Position = uCamera.viewProjection * vec4(aPos, 1.0);
    fragColor = aColor;
    fragUV = aUV; 
}

// =================================================================================================

#type fragment

#version 450 core

layout (location = 0) in vec4 fragColor;
layout (location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set=2, binding=0 ) uniform sampler2D uTexture0; // see comment of SDL_CreateGPUShader, the set is the rule of SDL3!!!

void main() 
{
    outColor = texture(uTexture0, fragUV) * fragColor;
}
