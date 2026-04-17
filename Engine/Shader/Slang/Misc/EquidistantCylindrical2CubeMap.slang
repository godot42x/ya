#include "Common/Helper.slang"


struct PushConstants {
    uint faceIndex;
    uint flipVertical;
};

[[vk::push_constant]] PushConstants pc;


struct VertexOutput{
    vec2 uv : TEXCOORD0;
    vec4 sv_position : SV_Position;
};

struct GeometryOutput {
    vec2 uv : TEXCOORD0;
    vec4 sv_position : SV_Position;
    uint layer : SV_RenderTargetArrayIndex;
};



[[vk::binding(0,0)]] Sampler2D uTexEquidistantRectangular;

[shader("vertex")]
void vertMain(uint vertexId : SV_VertexID, out VertexOutput OUT)
{
    float2 pos;
    if (vertexId == 0) {
        pos = float2(-1.0, -1.0);
    }
    else if (vertexId == 1) {
        pos = float2(3.0, -1.0);
    }
    else {
        pos = float2(-1.0, 3.0);
    }

    OUT.uv          = pos * 0.5 + 0.5;
    OUT.sv_position = float4(pos, 0.0, 1.0);
}

[shader("geometry")]
[maxvertexcount(3)]
void geomMain(triangle VertexOutput IN[3], inout TriangleStream<GeometryOutput> outStream)
{
    for (int i = 0; i < 3; ++i) {
        GeometryOutput OUT;
        OUT.uv          = IN[i].uv;
        OUT.sv_position = IN[i].sv_position;
        OUT.layer       = pc.faceIndex;
        outStream.Append(OUT);
    }
    outStream.RestartStrip();
}


struct FragmentOutput{
    vec4 color: SV_Target;
};

// atan / asin 的输出范围分别是 [-pi, pi] 和 [-pi/2, pi/2]。
// 乘以 1 / (2pi) 与 1 / pi 后再平移到 [0, 1]，即可得到经纬展开图的 UV。
static const vec2 invAtan = vec2(0.1591,0.3183);
vec3 cubemapFaceDirection(uint faceIndex, vec2 uv)
{
    float2 st = uv * 2.0 - 1.0;
    st.y      = -st.y;

    switch (faceIndex) {
    case 0: return normalize(vec3( 1.0,  st.y, -st.x));
    case 1: return normalize(vec3(-1.0,  st.y,  st.x));
    case 2: return normalize(vec3( st.x,  1.0, -st.y));
    case 3: return normalize(vec3( st.x, -1.0,  st.y));
    case 4: return normalize(vec3( st.x,  st.y,  1.0));
    default: return normalize(vec3(-st.x, st.y, -1.0));
    }
}

vec2 sampleSphericalMap(vec3 viewDir)
{
    // xz 平面角度对应经度，y 分量对应纬度。
    vec2 uv = vec2(atan2(viewDir.z, viewDir.x), asin(viewDir.y));
    uv *=invAtan;
    uv+= 0.5;
    return uv;
}

[shader("fragment")]
void fragMain(in GeometryOutput IN, out FragmentOutput OUT)
{
    // 直接由屏幕 uv 解析出当前 cubemap face 的方向，再映射到等距圆柱图上取样。
    vec2 uv = sampleSphericalMap(cubemapFaceDirection(pc.faceIndex, IN.uv));
    if (pc.flipVertical != 0) {
        uv.y = 1.0 - uv.y;
    }
    OUT.color = vec4(uTexEquidistantRectangular.Sample(uv).xyz, 1.0);
}