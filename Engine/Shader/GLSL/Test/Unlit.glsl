#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec3 aNormal;

//every frame update once
layout(set =0, binding =0, std140) uniform FrameUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint frameIdx;
    float time;
} uFrame;

//every material update once
// layout(set =1, binding =0, std140) uniform MaterialUBO {
//     vec3 baseColor0;
//     vec3 baseColor1;
//     float mixValue;
//     TextureParam textureParam0;
//     TextureParam textureParam1;
// } uMaterial;


//every draw call once (3d mesh with material)
layout(push_constant) uniform PushConstants {
    mat4 modelMat;
    mat3 normalMat;
} pc;


layout(location = 1) out vec2 vTexcoord;

// out VertexOutput {
//     vec2 texcoord;
// } vOut;

void main (){
    gl_Position = uFrame.projMat * uFrame.viewMat * pc.modelMat * vec4(aPos, 1.0);
// #if YA_PLATFORM_VULKAN
//     gl_Position.y = -gl_Position.y; // flip y for vulkan
// #endif
    // vOut.texcoord = aTexcoord; // 
    vTexcoord = aTexcoord;
}


#type fragment
#version 450

struct TextureParam{
    bool bEnable;
    float uvRotation;
    vec4  uvTransform; // x,y: scale, z,w: translate
};


layout (location=1) in vec2 vTexcoord;

layout(set =0, binding =0, std140) uniform FrameUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint frameIdx;
    float time;
} uFrame;

// every material update once
layout(set =1, binding =0, std140) uniform MaterialUBO {
    vec3 baseColor0;
    vec3 baseColor1;
    float mixValue;
    TextureParam textureParam0;
    TextureParam textureParam1;
} uMaterial;

// every material update once
layout(set =2, binding = 0) uniform sampler2D uTexture0;
layout(set =2, binding = 1) uniform sampler2D uTexture1;


layout(location = 0)    out  vec4 fColor;



vec2 getTextureUV(TextureParam param, vec2 inUV)
{
    // scale
    vec2 ret = inUV * param.uvTransform.xy; 
    // rotate
    ret = vec2(
        ret.x * sin(param.uvRotation) + ret.y * cos(param.uvRotation),
        ret.y * sin(param.uvRotation) + ret.x * cos(param.uvRotation)
    ); 
    // translate
    ret += param.uvTransform.zw;
    return ret;
}

void main(){
    vec3 color0 = uMaterial.baseColor0;
    vec3 color1 = uMaterial.baseColor1;

    if(uMaterial.textureParam0.bEnable){
        TextureParam param = uMaterial.textureParam0;
        // float alpha = sin(uFrame.time) * 0.5 + 0.5;
        param.uvTransform.w = - uFrame.time;
        vec2 uv0 = getTextureUV(param, vTexcoord);
        color0 = texture(uTexture0, uv0).rgb;
    }

    if(uMaterial.textureParam1.bEnable){
        color1 = texture(uTexture1, getTextureUV(uMaterial.textureParam1, vTexcoord)).rgb;
    }


    fColor = vec4(mix(color0, color1, uMaterial.mixValue), 1.0);

}
