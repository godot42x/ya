#type vertex
#version 450

layout (location = 0) in vec3 aPos;

layout (push_constant, std140) uniform PushConstant
{
    mat4 model;
} pc;

void main()
{
    gl_Position =   pc.model * vec4(aPos, 1.0);

}

// MARK: ===========

#type geometry
#version 450

#include "Common/Limits.glsl"

layout(triangles) in;
layout(triangle_strip, max_vertices = (MAX_POINT_LIGHTS * 6 +1) *3) out;

layout(location =0) out flat int pointLightIndex;
layout(location =1) out vec4 fragPos;

struct PointLightData{
    mat4 matrix[6];
    vec3 pos;
    float farPlane;

};

layout (set =0, binding = 0) uniform FrameData{
    mat4 directionalLightMatrix;
    PointLightData pointLights[ MAX_POINT_LIGHTS];
    uint numPointLights;
    uint hasDirectionalLight; // TODO: use macro to cut branch
} uFrame;

void main()
{

    // directional light shadow depth
    if (uFrame.hasDirectionalLight == 1) {
        gl_Layer = 0;
        for (int vertIdx = 0; vertIdx < 3; ++vertIdx)
        {
            vec4 worldPos = gl_in[vertIdx].gl_Position;
            gl_Position = uFrame.directionalLightMatrix * worldPos;
            pointLightIndex = -1;
            fragPos = worldPos;
            EmitVertex();
        }
        EndPrimitive();
    }

    // point light shadow depths
    for (uint lightIdx = 0; lightIdx < uFrame.numPointLights; ++lightIdx)
    {
        // pos_x -> neg_x -> pos_y -> neg_y -> pos_z -> neg_z
        for (uint faceIdx = 0; faceIdx < 6; ++faceIdx)
        {
            uint index = lightIdx * 6 + faceIdx;
            gl_Layer  = int(1+index);
            for (uint vertIdx = 0; vertIdx < 3; ++vertIdx)
            {
                vec4 worldPos = gl_in[vertIdx].gl_Position;
                fragPos = worldPos;
                gl_Position = uFrame.pointLights[lightIdx].matrix[faceIdx] * worldPos;
                pointLightIndex =  int(lightIdx);
                EmitVertex();
            }
            EndPrimitive();
        }
    }
}

//MARK: ===========
#type fragment
#version 450

#include "Common/Limits.glsl"

struct PointLightData{
    mat4 matrix[6];
    vec3 pos;
    float farPlane;
};

layout (set =0, binding = 0) uniform FrameData{
    mat4 directionalLightMatrix;
    PointLightData pointLights[ MAX_POINT_LIGHTS];
    uint numPointLights;
    uint hasDirectionalLight; // TODO: use macro to cut branch
} uFrame;

layout(location =0) in flat int pointLightIndex;
layout(location =1) in vec4 fragPos;


void main()
{
    if(pointLightIndex == -1) {
        // directional light: use hardware depth
        gl_FragDepth = gl_FragCoord.z;
    } else {
        // point light: use linear distance
        float lightDistance = length( fragPos.xyz - uFrame.pointLights[pointLightIndex].pos);
        lightDistance = lightDistance / uFrame.pointLights[pointLightIndex].farPlane;
        gl_FragDepth =  lightDistance;
    }
}