#type vertex

#version 450 core

// layout(location = 0) in vec3 aPos;

void main()
{
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}


#type fragment

#version 450 core

layout(location = 0) out vec4 outColor;

void main() 
{
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
