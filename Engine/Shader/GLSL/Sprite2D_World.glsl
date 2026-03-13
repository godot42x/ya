#type vertex

#version 450 core

layout(location = 0) in vec3 aPosition; 
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in uint aTextureIdx;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 camViewProj;
} uFrame;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexcoord;
layout(location = 2) out flat uint vTextureIdx;

void main()
{
    gl_Position = uFrame.camViewProj * vec4(aPosition, 1.0);
    vColor = aColor;
    // World-space quads have Y+ pointing up, but the quad's vertex layout
    // maps increasing Y to increasing V (texture bottom). Flip V so the
    // texture is right-side-up when rendered in world space.
    vTexcoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);
    vTextureIdx = aTextureIdx;
}

// =================================================================================================
#type fragment

#version 450 core

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord;
layout(location = 2) in flat uint vTextureIdx;

#ifndef TEXTURE_SET_SIZE
#define TEXTURE_SET_SIZE 32
#endif

// 0 is a white texture
layout(set = 1, binding = 0) uniform sampler2D uTextures[TEXTURE_SET_SIZE];

layout(location = 0) out vec4 fColor;

void main() 
{
    int textureIndex = int(vTextureIdx);
    vec4 texColor = texture(uTextures[textureIndex], vTexcoord);
    // png eg. has transparent areas
    if(texColor.a < 0.01){
        discard;
    }

    fColor = texColor * vColor;
}
