import Common.Helper;

// Precompute
// Irradiance convolution computing
// sample 1 face of a cubemap use kernel to get the irradiance map (辐照度)
// handle 1 face by 1 drawcall(off-screen rendering)

struct PushConstants{
    mat4 view;
    mat4 projection;
    uint faceIndex;
};

[[vk::push_constant]] PushConstants pc;

struct VertexInput{
    vec3 pos: POSITION;
};

struct VertexOutput{
    vec4 clipPos : SV_Position;
    vec3 localPos: TEXCOORD0;
};


[shader("vertex")]
void vertMain(in VertexInput IN, out VertexOutput OUT)
{
    OUT.clipPos = mul(pc.projection, mul(pc.view, vec4(IN.pos, 1.0)));
    OUT.localPos = IN.pos;
}

[[vk::binding(0,0)]] SamplerCube environmentMap;

[shader("fragment")]
vec4 fragMain(VertexOutput IN) : SV_Target
{
    vec3 N = normalize(IN.localPos);

    vec3 up = abs(N.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    vec3 ret = vec3(0);
    float sampleDelta = 0.025;
    float sampleCount = 0;
    // sample each direction on hemisphere surface
    for (float phi = 0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // spherical to cartesian
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // tangent space to world space
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            // sample the environment map
            vec3 irradiance = environmentMap.Sample(sampleVec).rgb * cos(theta) * sin(theta);

            // accumulate
            ret += irradiance;
            sampleCount += 1.0;
        }
    }

    ret = PI * ret * (1.0 / max(sampleCount, 1.0));

    return vec4(ret, 1.0);
}