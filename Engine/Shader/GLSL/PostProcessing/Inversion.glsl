#type vertex
#version 450

// in vulkan
const vec4 quadVertices[4] = vec4[4](
    vec4(-1.0, -1.0, 0.0, 1.0),  // 0: 左上
    vec4( 1.0, -1.0, 0.0, 1.0),  // 1: 右上
    vec4(-1.0,  1.0, 0.0, 1.0),  // 2: 左下
    vec4( 1.0,  1.0, 0.0, 1.0)   // 3: 右下
);

const int quadIndices[6] = int[6](
    0, 1, 2, 1, 3, 2
);

const vec2 quadTexCoords[4] = vec2[4](
    vec2(0.0, 0.0),  // 0: 左上
    vec2(1.0, 0.0),  // 1: 右上
    vec2(0.0, 1.0),  // 2: 左下
    vec2(1.0, 1.0)   // 3: 右下
);

#define DEBUG_ALL 0

layout(location = 0) out vec2 vTexCoord;
#if DEBUG_ALL
    layout(location = 1) out vec3 vColor;
#endif



void main()
{
    int index = quadIndices[gl_VertexIndex];
    vec4 vertex = quadVertices[index];

    // gl to vulkan ndc
    vertex.y = -vertex.y;

    // ver

    gl_Position = vertex;
#if DEBUG_ALL
    vColor = index == 0 ? vec3(1.0, 0.0, 0.0) : 
            index == 1 ? vec3(0.0, 1.0, 0.0) :
            index == 2 ? vec3(0.0, 0.0, 1.0) :
                    vec3(0.0, 0.0, 0.0);
#endif

    // Use quadTexCoords array instead of computing from NDC
    // reverse uv?
    vec2 uv = quadTexCoords[index];
    uv.y = 1.0 - uv.y;
    vTexCoord = uv;

}

#type fragment
#version 450

layout(location = 0) in vec2 vTexCoord;

#if DEBUG_ALL
    layout(location = 1) in vec3 vColor;
#endif

layout(location = 0) out vec4 fColor;

layout(set = 0, binding = 0) uniform sampler2D uScreenTexture;

void main(){
    #if DEBUG_ALL
        fColor = vec4(vColor, 1.0);
        return;
    #endif

    vec3 color = texture(uScreenTexture, vTexCoord).rgb;
    // Invert colors
    fColor = vec4(1.0 - color, 1.0);
    
}