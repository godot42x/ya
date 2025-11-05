#type vertex
#version 450

layout(location = 0) in vec4 aPosUV; // xy: pos, zw: uv

layout(location = 0) out vec2 vUV;

layout (set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
} uFrame;


void main() {
    gl_Position = uFrame.projection * vec4(aPosUV.xy, 0.0, 1.0);
    vUV = aPosUV.zw;
}


#type fragment
#version 450

layout(set = 1, binding = 0) uniform sampler2D uTextureFontAtlas; // baked font texture

layout (push_constant) uniform PushConst {
    vec3 color
} pc;


layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fColor;

void main() {

    float alpha = texture(uTextureFontAtlas, vUV).a; // GL_RED or SDF 单通道

    vec4 texColor = texture(uTextureFontAtlas, vUV);
    // png eg. has transparent areas
    if(texColor.a < 0.01){
        discard;
    }
    fColor = texColor * vec4(pc.color, alpha);
}