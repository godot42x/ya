#type vertex

#version 450 core

layout (location = 0) in vec4 aPos;
layout (location = 1) in vec4 aColor;

vec4 vectors[3] = vec4[3](
    vec4(0.0,0.5, 0.0, 1.0),
    vec4(-0.5, -0.5, 0.0, 1.0),
    vec4(+0.5, -0.5, 0.0, 1.0)
);
vec4 colors[3] = vec4[3](vec4(1.0, 0.0, 0.0, 1.0), vec4(0.0, 1.0, 0.0, 1.0), vec4(0.0, 0.0, 1.0, 1.0));


layout(location = 0) out vec4 fragColor;

void main()
{
    gl_Position = vectors[gl_VertexIndex];
    fragColor = colors[gl_VertexIndex];
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
