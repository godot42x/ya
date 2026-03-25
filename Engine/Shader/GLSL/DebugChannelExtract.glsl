#type vertex
#version 450

const vec4 quadVertices[4] = vec4[4](
    vec4(-1.0, -1.0, 0.0, 1.0),
    vec4( 1.0, -1.0, 0.0, 1.0),
    vec4(-1.0,  1.0, 0.0, 1.0),
    vec4( 1.0,  1.0, 0.0, 1.0)
);

const int quadIndices[6] = int[6](0, 1, 2, 1, 3, 2);

const vec2 quadTexCoords[4] = vec2[4](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0)
);

layout(location = 0) out vec2 vTexCoord;

// push_constant: which channel to extract (0=R, 1=G, 2=B, 3=A)
layout(push_constant) uniform PC {
    int channel; // 0=R, 1=G, 2=B, 3=A
} pc;

void main()
{
    int index = quadIndices[gl_VertexIndex];
    vec4 vertex = quadVertices[index];
    vertex.y = -vertex.y;
    gl_Position = vertex;

    vec2 uv = quadTexCoords[index];
    uv.y = 1.0 - uv.y;
    vTexCoord = uv;
}

// MARK: fragment
#type fragment
#version 450

layout(location = 0) in vec2 vTexCoord;

layout(push_constant) uniform PC {
    int channel;
} pc;

layout(location = 0) out vec4 fColor;

layout(set = 0, binding = 0) uniform sampler2D uInputTexture;

void main()
{
    vec4 texel = texture(uInputTexture, vTexCoord);
    float val = 0.0;

    // pc.channel: 0=R, 1=G, 2=B, 3=A
    if (pc.channel == 0)      val = texel.r;
    else if (pc.channel == 1) val = texel.g;
    else if (pc.channel == 2) val = texel.b;
    else                      val = texel.a;

    fColor = vec4(vec3(val), 1.0);
}
