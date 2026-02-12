#type vertex
#version 450

layout(location =0) in vec3 aPos;

layout(location =0) out vec3 vUV;


layout(set = 0, binding = 0, std140)  uniform FrameUBO{
    mat4 proj;
    mat4 view;

} uFrame;


void main()
{
    vUV = aPos;
    gl_Position = uFrame.proj * uFrame.view * vec4(aPos, 1.0);
    gl_Position.z =  gl_Position.w;
    
}

// MARK: Separator
#type fragment

#version 450

layout(location = 0) in vec3 aUV;

layout(location = 0) out vec4 fColor;

layout(set =1 , binding = 0) uniform samplerCube  uSkyBox;


void main(){
    fColor = texture(uSkyBox, aUV);
}
