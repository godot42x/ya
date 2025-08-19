#type vertex

#version 450 core

layout(location = 0) in vec3 aPosition; 
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aTextureId;
layout(location = 4) in float aRotation;

// layout(set = 0, binding = 0) uniform CameraData {
//     mat4 projectionMatrix;
// } uCamera;
layout(push_constant) uniform CameraData {
    mat4 camProj; // Camera projection matrix
} pc;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out float fragTextureId;

void main()
{
    gl_Position = pc.camProj * vec4(aPosition, 1.0);
    fragColor = aColor;
    fragTexCoord = aTexCoord;
    fragTextureId = aTextureId;
}

// =================================================================================================

#type fragment

#version 450 core

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in float fragTextureId;

layout(set = 1, binding = 0) uniform sampler2D uTextures[32];

layout(location = 0) out vec4 outColor;

void main() 
{
    int textureIndex = int(fragTextureId);
    
    if (textureIndex == 0) {
        // White texture (solid color)
        outColor = fragColor;
    } else {
        // Sample from texture array
        vec4 texColor = texture(uTextures[textureIndex], fragTexCoord);
        outColor = texColor * fragColor;
    }
}

