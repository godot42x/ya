#type vertex

#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aUV; // aka aTexCoord

// vec3 vectors[3] = vec3[3](
//     vec3(0.0,   0.5, 0.0),
//     vec3(-0.5, -0.5, 0.0),
//     vec3(+0.5, -0.5, 0.0)
// );
// vec4 colors[3] = vec4[3](vec4(1.0, 0.0, 0.0, 1.0), vec4(0.0, 1.0, 0.0, 1.0), vec4(0.0, 0.0, 1.0, 1.0));


layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

void main()
{
    // gl_Position = vec4(vectors[gl_VertexIndex],1);
    // fragColor = colors[gl_VertexIndex];
    gl_Position = vec4(aPos, 1); // so there was a default orthographic camera matrix to multiply?
    fragColor = aColor;
    fragUV = aUV; 
}

// =================================================================================================

#type fragment

#version 450 core

layout (location = 0) in vec4 fragColor;
layout (location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set=2, binding=0 ) uniform sampler2D uTexture0; // see comment of SDL_CreateGPUShader, the set is the rule of SDL3!!!

void main() 
{
    outColor = texture(uTexture0, fragUV) * fragColor;
}
