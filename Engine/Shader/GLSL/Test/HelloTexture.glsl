#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aNormal;



layout(set=0, binding =0, std140) uniform  GBuffer{
    mat4 projMat;
    mat4 viewMat;
}uGBuffer;

layout(set=0, binding =1, std140) uniform  InstanceBuffer{
    mat4 modelMat;
} uInstanceBuffer;


out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexCoord;

void main()
{
    // Create different positions for each instance
    vec3 instanceOffset = vec3(
        float(gl_InstanceIndex % 3) * 3.0 - 3.0,  // X: -3, 0, 3
        float(gl_InstanceIndex / 3) * 3.0 - 3.0,  // Y: -3, 0, 3  
        0.0
    );
    
    // 为每个实例创建独立的Model矩阵
    mat4 instanceModel = uInstanceBuffer.modelMat;
    instanceModel[3].xyz += instanceOffset;
    
    //  vulkan origin point at left-top, opengl origin point at left-bottom
    // if you want to flip the Y axis, you can use this:
    // instanceModel[1][1] *= -1.0;
    vec4 worldPos = uGBuffer.projMat*  uGBuffer.viewMat * instanceModel * vec4(aPos, 1.0);
    gl_Position =  worldPos;

    
    vec3 normal = normalize(aNormal.xyz);

    // if (abs(normal.x) > 0.9) {
    //     // Left/Right faces (X axis)
    //     vColor = normal.x > 0.0 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.5, 0.0, 0.0, 1.0); // Red/Dark Red
    // } else if (abs(normal.y) > 0.9) {
    //     // Top/Bottom faces (Y axis)
    //     vColor = normal.y > 0.0 ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(0.0, 0.5, 0.0, 1.0); // Green/Dark Green
    // } else {
    //     // Front/Back faces (Z axis)
    //     vColor = normal.z > 0.0 ? vec4(0.0, 0.0, 1.0, 1.0) : vec4(0.0, 0.0, 0.5, 1.0); // Blue/Dark Blue
    // }
    vColor = vec4(1.0, 1.0, 1.0, 1.0);

    vTexCoord = aTexCoord;
}

// token: split-stage

#type fragment
#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord;

layout(set=0, binding = 2) uniform  sampler2D uTexture0;
layout(set=0, binding = 3) uniform  sampler2D uTexture1;
layout(set=0, binding = 4) uniform  sampler2D uTExtures[16];


layout(push_constant) uniform PushConstants {
    float textureMixAlpha;
} pc;

layout(location = 0) out vec4 outColor;


void main()
{
    // Output the color directly
    outColor = vColor * mix(
        texture(uTexture0, vTexCoord),
        texture(uTexture1, vTexCoord),
        pc.textureMixAlpha
    );

}