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
layout(location = 1) out flat uint vEffect;

#if DEBUG_ALL
layout(location = 1) out vec3 vColor;
#endif


const uint INVERSION = 0;
const uint GRAYSCALE = 1;
const uint WEIGHTED_GRAYSCALE = 2;
const uint KERNEL_SHARPEN = 3;
const uint KERNEL_BLUR = 4;
const uint KERNEL_EDGE_DETECTION = 5;
const uint TONE_Mapping = 6;
const uint RANDOM = 7;

layout (push_constant) uniform PushConstants {
    uint effect; 
} pc;

const uint EFFECT_BEGIN = INVERSION;
const uint EFFECT_END = RANDOM;


void main()
{
    int index = quadIndices[gl_VertexIndex];
    vec4 vertex = quadVertices[index];
    vertex.y = -vertex.y; // reverse y
    gl_Position = vertex;

    vec2 uv = quadTexCoords[index];
    uv.y = 1.0 - uv.y; // also reverse uv y
    vTexCoord = uv;

    
    vEffect = clamp(pc.effect, EFFECT_BEGIN, EFFECT_END);



#if DEBUG_ALL
    vColor =index == 0 ? vec3(1.0, 0.0, 0.0) : 
            index == 1 ? vec3(0.0, 1.0, 0.0) :
            index == 2 ? vec3(0.0, 0.0, 1.0) :
                         vec3(0.0, 0.0, 0.0);
#endif
}

#type fragment
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in flat uint vEffect;

#if DEBUG_ALL
    layout(location = 1) in vec3 vColor;
#endif

layout (push_constant) uniform FragPushConstants {
    layout(offset = 16) vec4 floatParams[4];  // offset must match Pipeline Layout (after vertex PC)
} fragPC;


const uint INVERSION = 0;
const uint GRAYSCALE = 1;
const uint WEIGHTED_GRAYSCALE = 2;
const uint KERNEL_SHARPEN = 3;
const uint KERNEL_BLUR = 4;
const uint KERNEL_EDGE_DETECTION = 5;
const uint TONE_Mapping = 6;
const uint RANDOM = 7;


layout(location = 0) out vec4 fColor;

layout(set = 0, binding = 0) uniform sampler2D uScreenTexture;




void main(){
    #if DEBUG_ALL
        fColor = vec4(vColor, 1.0);
        return;
    #endif

    switch (vEffect) 
    {
    case INVERSION: 
    {
        vec3 color = texture(uScreenTexture, vTexCoord).rgb;
        fColor = vec4(1.0 - color, 1.0); // Invert colors
    }break;
    case GRAYSCALE: 
    {
        vec4 color = texture(uScreenTexture, vTexCoord);
        float average = (color.r + color.g + color.b) / 3.0;
        fColor = vec4(average, average, average, 1.0);
    } break;
    case WEIGHTED_GRAYSCALE:{
        vec4 color = texture(uScreenTexture, vTexCoord);
        float average = 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
        fColor = vec4(average, average, average, 1.0);
    } break;
    case  KERNEL_SHARPEN:
    {
        // const float offset = 1.0 / 300.0;
        const float offset = fragPC.floatParams[0].x;
        const vec2 offsets[9] = vec2[](
            vec2(-offset,  offset), // 左上
            vec2( 0.0f,    offset), // 正上
            vec2( offset,  offset), // 右上
            vec2(-offset,  0.0f),   // 左
            vec2( 0.0f,    0.0f),   // 中
            vec2( offset,  0.0f),   // 右
            vec2(-offset, -offset), // 左下
            vec2( 0.0f,   -offset), // 正下
            vec2( offset, -offset)  // 右下
        );
        const float kernel[9] = float[](
            -1.0, -1.0, -1.0,
            -1.0,  9.0, -1.0,
            -1.0, -1.0, -1.0
        );
        vec3 sampledTexture[9];
        for (int i = 0; i < 9; i++) {
            sampledTexture[i] = texture(uScreenTexture, vTexCoord + offsets[i]).rgb;
        }
        vec3 color = vec3(0.0);
        for (int i = 0; i < 9; i++) {
            color += sampledTexture[i] * kernel[i];
        }
        fColor = vec4(color, 1.0);
    } break;
    case KERNEL_BLUR:
    {
        const float offset = fragPC.floatParams[0].x;
        const vec2 offsets[9] = vec2[](
            vec2(-offset,  offset), // 左上
            vec2( 0.0f,    offset), // 正上
            vec2( offset,  offset), // 右上
            vec2(-offset,  0.0f),   // 左
            vec2( 0.0f,    0.0f),   // 中
            vec2( offset,  0.0f),   // 右
            vec2(-offset, -offset), // 左下
            vec2( 0.0f,   -offset), // 正下
            vec2( offset, -offset)  // 右下
        );

        const float alpha = fragPC.floatParams[0].y > 0.0 ? fragPC.floatParams[0].y : 16.0;
        const float kernel[9] = float[](
            1.0 / alpha, 2.0 / alpha, 1.0 / alpha,
            2.0 / alpha, 4.0 / alpha, 2.0 / alpha,
            1.0 / alpha, 2.0 / alpha, 1.0 / alpha  
        );
        vec3 sampledTexture[9];
        for (int i = 0; i < 9; i++) {
            sampledTexture[i] = texture(uScreenTexture, vTexCoord + offsets[i]).rgb;
        }
        vec3 color = vec3(0.0);
        for (int i = 0; i < 9; i++) {
            color += sampledTexture[i] * kernel[i];
        }
        fColor = vec4(color, 1.0);
    } break;
    case RANDOM:{
        // Do nothing
    } break;
    default: 
        break;
    }
    
}