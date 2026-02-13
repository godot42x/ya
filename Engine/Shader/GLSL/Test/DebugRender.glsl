#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec3 aNormal;

layout(set = 0, binding = 0, std140) uniform DebugUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint mode;
    float time;
    vec4 floatParam;
} uDebug;

layout(push_constant) uniform PushConstants {
    mat4 modelMat;
} pc;

layout(location = 0) out vec2 vTexcoord;
layout(location = 1) out vec3 vWorldNormal;

void main()
{
    vec4 worldPos = pc.modelMat * vec4(aPos, 1.0);
    gl_Position = uDebug.projMat * uDebug.viewMat * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(pc.modelMat)));
    vWorldNormal = normalize(normalMatrix * aNormal);
    vTexcoord = aTexcoord;
}

#type fragment
#version 450

layout(set = 0, binding = 0, std140) uniform DebugUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint mode;
    float time;
    vec4 floatParam;
} uDebug;

layout(location = 0) in vec2 vTexcoord;
layout(location = 1) in vec3 vWorldNormal;

layout(location = 0) out vec4 fColor;

void main()
{
    if (uDebug.mode == 1u) {
        fColor = vec4(normalize(vWorldNormal) * 0.5 + 0.5, 1.0);
        return;
    }

    if (uDebug.mode == 2u) {
        const float nearZ = 0.1;
        const float farZ = 100.0;
        float d = gl_FragCoord.z;
        d = d * 2.0 - 1.0;
        d = (2.0 * nearZ * farZ) / ((farZ + nearZ) - d * (farZ - nearZ));
        d = (d - nearZ) / (farZ - nearZ);
        fColor = vec4(vec3(d), 1.0);
        return;
    }

    if (uDebug.mode == 3u) {
        fColor = vec4(vTexcoord.x, vTexcoord.y, 0.0, 1.0);
        return;
    }

    discard;
}
