#type vertex

#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;

vec3 vectors[3] = vec3[3](
    vec3(0.0,   0.5, 0.0),
    vec3(-0.5, -0.5, 0.0),
    vec3(+0.5, -0.5, 0.0)
);
vec4 colors[3] = vec4[3](vec4(1.0, 0.0, 0.0, 1.0), vec4(0.0, 1.0, 0.0, 1.0), vec4(0.0, 0.0, 1.0, 1.0));


layout(location = 0) out vec4 fragColor;

void main()
{
    // gl_Position = vec4(vectors[gl_VertexIndex],1);
    // fragColor = colors[gl_VertexIndex];
    gl_Position = vec4(aPos, 1); // so there was a default orthographic camera matrix to multiply?
    fragColor = aColor;
}

// =================================================================================================

#type fragment

#version 450 core

layout (location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() 
{
    outColor = fragColor;
}
