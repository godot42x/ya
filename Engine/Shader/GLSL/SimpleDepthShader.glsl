#type vertex
#version 450

layout (location = 0) in vec3 aPos;


layout (set =0, binding = 0) uniform FrameData{
    mat4 lightMatrix;
} uFrame;

layout (push_constant, std140) uniform PushConstant
{
    mat4 model;
} pc;

void main()
{
    gl_Position =  uFrame.lightMatrix * pc.model * vec4(aPos, 1.0);

}


//MARK: ===========
#type fragment
#version 450

void main()
{
    // do nothing
}