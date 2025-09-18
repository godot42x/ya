#type vertex

#version 450 core

layout(location = 0) in vec3 aPosition; 
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in uint aTextureIdx;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 matViewProj;
} uFrame;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexcoord;
layout(location = 2) out flat uint vTextureIdx;

void main()
{
    gl_Position = uFrame.matViewProj * vec4(aPosition, 1.0);
    vColor = aColor;
    vTexcoord = aTexCoord;
    vTextureIdx = aTextureIdx;
}

// =================================================================================================
#type fragment

#version 450 core

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord;
layout(location = 2) in flat uint vTextureIdx;

// 0 is a white texture
layout(set = 1, binding = 0) uniform sampler2D uTextures[32];

layout(location = 0) out vec4 fColor;

void main() 
{
    fColor = vColor;
    int textureIndex = int(vTextureIdx);
    
    vec4 texColor = texture(uTextures[textureIndex], vTexcoord);
    fColor = texColor * vColor;
}

