#type vertex

#version 450 core

layout(location = 0) in vec3 aPosition; 
// layout(location = 1) in vec2 aTexCoord;
layout(location = 1) in vec4 aColor;
// layout(location = 3) in float aTextureId;
layout(location = 2) in vec2 aTexCoord;

// layout(set = 0, binding = 0) uniform CameraData {
//     mat4 projectionMatrix;
// } uCamera;
// layout(push_constant) uniform CameraData {
//     mat4 camProj; // Camera projection matrix
// } pc;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexcoord;
// layout(location = 2) out float vTextureID;

// a cube
// vec3 quad[4] = {
//     vec3(0, 0, 0),
//     vec3(1, 0, 0),
//     vec3(0, 1, 0),
//     vec3(1, 1, 0),
// };

void main()
{
    gl_Position =// pc.camProj *
     vec4(aPosition, 1.0);
    // gl_Position = vec4(quad[gl_VertexIndex], 1.0);
    vColor = aColor;
    vTexcoord = aTexCoord;
    // vTextureID = aTextureId;
}

// =================================================================================================
#type fragment

#version 450 core

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord;
// layout(location = 2) in float vTextureID;

// layout(set = 1, binding = 0) uniform sampler2D uTextures[32];

layout(location = 0) out vec4 fColor;

void main() 
{
    fColor = vColor;
    // int textureIndex = int(vTextureID);
    
    // if (textureIndex == 0) {
        // White texture (solid color)
    //     outColor = vColor;
    // } else {
        // Sample from texture array
    //     vec4 texColor = texture(uTextures[textureIndex], vTexcoord);
    //     outColor = texColor * vColor;
    // }
}

