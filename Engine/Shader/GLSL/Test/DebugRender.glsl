#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec3 aNormal;

layout(set = 0, binding = 0, std140) uniform DebugUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    int mode;
    float time;
    vec4 floatParam;
} uDebug;

layout(push_constant) uniform PushConstants {
    mat4 modelMat;
} pc;

layout(location = 0) out vec2 vTexcoord;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec3 vWorldPos;

void main()
{
    vec4 pos = pc.modelMat * vec4(aPos, 1.0);
    vWorldPos = pos.xyz;
    gl_Position = uDebug.projMat * uDebug.viewMat * pos;

    mat3 normalMatrix = transpose(inverse(mat3(pc.modelMat)));
    vWorldNormal = normalize(normalMatrix * aNormal);
    vTexcoord = aTexcoord;
}

// MARK: ==============
#type geometry
#version 450

layout(location = 0) in vec2 vTexcoord[];
layout(location = 1) in vec3 vWorldNormal[];

layout(location = 0) out vec2 gTexcoord;
layout(location = 1) out vec3 gWorldNormal;
layout(location = 2) out vec4 gColor;

layout(set = 0, binding = 0, std140) uniform DebugUBO {
    mat4  projMat;
    mat4  viewMat;
    ivec2 resolution;
    int mode;
    float time;
    vec4  floatParam;
} uDebug;


layout(triangles) in;

#ifdef DEBUG_NORMAL_DIR

    layout(location = 2) in vec3 vWorldPos[];

    layout(line_strip, max_vertices = 12) out;

    void generateLine(vec4 start, vec4 end, vec4 color)
    {
        gl_Position = start;
        gColor = color;
        EmitVertex();

        gl_Position = end;
        gColor = color;
        EmitVertex();
        EndPrimitive();
    }

    void main()
    {
        const float length = 0.4;
        mat4 vp = uDebug.projMat * uDebug.viewMat;

        const vec4 color0 = vec4(1.0, 0.0, 0.0, 1.0);
        const vec4 color1 = vec4(0.0, 1.0, 0.0, 1.0);
        const vec4 color2 = vec4(0.0, 0.0, 1.0, 1.0);
        const vec4 color3 = vec4(0.8, 0.5, 0.02, 0.6); // "#edf102"

        const int bInTriangleCenter = 1;


        if(bInTriangleCenter == 1){
            vec4 center =  vec4((vWorldPos[0] + vWorldPos[1] + vWorldPos[2]) / 3.0, 1.0);

            generateLine(vp* center, vp * (center + vec4(1.0, 0.0, 0.0, 0.0) * length), color0); // x
            generateLine(vp* center, vp * (center + vec4(0.0, 1.0, 0.0, 0.0) * length), color1); // y
            generateLine(vp* center, vp * (center + vec4(0.0, 0.0, 1.0, 0.0) * length), color2); // z

            // in triangle center
            generateLine(vp * center, vp *(center + vec4(vWorldNormal[0], 0.0)) * length *2, color3);
        }
        else // in each vertex
        {
            for (int i = 0; i < 3; ++i) {
                vec4 start  = vp * vec4(vWorldPos[i], 1.0);
                vec4 end    = vp * vec4(vWorldPos[i] + vWorldNormal[i] * length, 1.0);
                generateLine(start, end, color3);
            }
        }

    }
#else
    // Default
    layout(triangle_strip, max_vertices = 3) out;

    void main()
    {

        for (int i = 0; i < 3; ++i) {
            gl_Position = gl_in[i].gl_Position;
            gTexcoord = vTexcoord[i];
            gWorldNormal = vWorldNormal[i];
            gColor = vec4(0.0);

            EmitVertex();
        }
        EndPrimitive();
    }

#endif


// MARK: ==============
#type fragment
#version 450

layout(set = 0, binding = 0, std140) uniform DebugUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    int mode;
    float time;
    vec4 floatParam;
} uDebug;

layout(location = 0) in vec2 gTexcoord;
layout(location = 1) in vec3 gWorldNormal;
layout(location = 2) in vec4 gColor;

layout(location = 0) out vec4 fColor;

void main()
{
    // normal color
    if (uDebug.mode == 1)
    {
        fColor = vec4(normalize(gWorldNormal) * 0.5 + 0.5, 1.0);
        return;
    }

    //  normal direction
    if (uDebug.mode == 2) {
        fColor =  gColor;
        return;
    }

    // depth
    if (uDebug.mode == 3) {
        const float nearZ = 0.1;
        const float farZ = 100.0;
        float d = gl_FragCoord.z;
        d = d * 2.0 - 1.0;
        d = (2.0 * nearZ * farZ) / ((farZ + nearZ) - d * (farZ - nearZ));
        d = (d - nearZ) / (farZ - nearZ);
        fColor = vec4(vec3(d), 1.0);
        return;
    }

    // UV
    if (uDebug.mode == 4) {
        fColor = vec4(gTexcoord.x, gTexcoord.y, 0.0, 1.0);
        return;
    }

    discard;
}
