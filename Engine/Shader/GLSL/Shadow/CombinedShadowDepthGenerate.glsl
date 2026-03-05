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

layout(triangles) in;
layout(triangle_strip, max_vertices = (MAX_POINT_LIGHTS * 6 +1) *3) out;

// layout (location = 0) out float gLinearDepth; // for directional light shadow depth

layout (set =0, binding = 0) uniform FrameData{
    mat4 directionalLightMatrix;
    mat4 pointLightMatrices[6 * MAX_POINT_LIGHTS];
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
            // gLinearDepth = gl_Position.z / gl_Position.w; // store linear depth in [0,1] range
            EmitVertex();
        }
        EndPrimitive();
    }

    // point light shadow depths
    for (uint lightIdx = 0; lightIdx < uFrame.numPointLights; ++lightIdx)
    {
        for (uint faceIdx = 0; faceIdx < 6; ++faceIdx)
        {
            gl_Layer = int(1 + lightIdx * 6 + faceIdx);
            for (uint vertIdx = 0; vertIdx < 3; ++vertIdx)
            {
                vec4 worldPos = gl_in[vertIdx].gl_Position;
                gl_Position = uFrame.pointLightMatrices[lightIdx * 6 + faceIdx] * worldPos;
                // gLinearDepth = gl_Position.z / gl_Position.w; // store linear depth in [0,1] range
                EmitVertex();
            }
            EndPrimitive();
        }
    }
}

//MARK: ===========
#type fragment
#version 450

void main()
{
    // do nothing, driver already do this before fragment shader:
    // gl_FragDepth = gl_FragCoord.z;
}