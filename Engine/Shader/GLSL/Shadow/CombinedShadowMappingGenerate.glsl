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

#ifndef MAX_POINT_LIGHTS
    #define MAX_POINT_LIGHTS 4
#endif

layout(triangles) in;
layout(triangle_strip, max_vertices = (MAX_POINT_LIGHTS * 6 +1) *3) out;

// layout (location = 0) out float gLinearDepth; // for directional light shadow depth

layout (set =0, binding = 0) uniform FrameData{
    mat4 directionalLightMatrix;
    mat4 pointLightMatrices[6 * MAX_POINT_LIGHTS];
    uint numPointLights;
    uint hasDirectionalLight; // TODO: use macro to cut branch
    float pointLightFarPlane;
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
    // for (uint lightIdx = 0; lightIdx < MAX_POINT_LIGHTS; ++lightIdx)
    {
        // pos_x -> neg_x -> pos_y -> neg_y -> pos_z -> neg_z
        for (uint faceIdx = 0; faceIdx < 6; ++faceIdx)
        {
            uint index = lightIdx * 6 + faceIdx;
            gl_Layer  = int(1+index);
            for (uint vertIdx = 0; vertIdx < 3; ++vertIdx)
            {
                vec4 worldPos = gl_in[vertIdx].gl_Position;
                gl_Position = uFrame.pointLightMatrices[index] * worldPos;
                // gLinearDepth = 1;
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
    // if(gl_Layer > 1) {
    //     gl_FragDepth = gl_Layer / (1 + 6* MAX_POINT_LIGHTS);
    // }
}