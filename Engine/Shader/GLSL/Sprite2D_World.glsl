#type vertex

#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in uint aTextureIdx;
layout(location = 4) in vec3 aWorldCenter;
layout(location = 5) in vec2 aWorldSize;
layout(location = 6) in uint aMode;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 camViewProj;
    mat4 camView;
} uFrame;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexcoord;
layout(location = 2) out flat uint vTextureIdx;
layout(location = 3) out flat uint vMode;

void main()
{
    vec3 cameraRight = normalize(mat3(uFrame.camView)[0]);
    vec3 cameraUp    = normalize(mat3(uFrame.camView)[1]);
    vec2 centered    = (aPosition.xy - vec2(0.5)) * aWorldSize;
    vec3 worldPos    = aWorldCenter + cameraRight * centered.x + cameraUp * centered.y;

    gl_Position = uFrame.camViewProj * vec4(worldPos, 1.0);
    vColor      = aColor;
    vTexcoord   = vec2(aTexCoord.x, 1.0 - aTexCoord.y);
    vTextureIdx = aTextureIdx;
    vMode       = aMode;
}

#type fragment

#version 450 core

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord;
layout(location = 2) in flat uint vTextureIdx;
layout(location = 3) in flat uint vMode;

#ifndef TEXTURE_SET_SIZE
#define TEXTURE_SET_SIZE 32
#endif

layout(set = 1, binding = 0) uniform sampler2D uTextures[TEXTURE_SET_SIZE];

layout(location = 0) out vec4 fColor;

void main()
{
    vec4 texColor = vec4(1.0);

    if (vMode == 1u) {
        vec2 sphereUv = vTexcoord * 2.0 - 1.0;
        float radius2 = dot(sphereUv, sphereUv);
        if (radius2 > 1.0) {
            discard;
        }

        float z      = sqrt(max(0.0, 1.0 - radius2));
        vec3 normal  = normalize(vec3(sphereUv.x, sphereUv.y, z));
        vec3 lightDir = normalize(vec3(-0.45, 0.55, 0.70));
        float diffuse = max(dot(normal, lightDir), 0.0);
        float rim     = pow(1.0 - max(normal.z, 0.0), 2.0);
        float shade   = 0.28 + diffuse * 0.62 + rim * 0.18;
        texColor      = vec4(vec3(shade), 0.92);
    }
    else {
        int textureIndex = int(vTextureIdx);
        texColor = texture(uTextures[textureIndex], vTexcoord);
        if (texColor.a < 0.01) {
            discard;
        }
    }

    fColor = texColor * vColor;
}