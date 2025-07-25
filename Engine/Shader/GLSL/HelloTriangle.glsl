#type vertex

#version 450 core

vec3 vertices[] = {
    vec3(-0.5f, -0.5f, 0.0f), // Bottom left
    vec3( 0.5f, -0.5f, 0.0f), // Bottom right
    vec3( 0.0f,  0.5f, 0.0f)  // Top
};

vec4 colors[] = {
    vec4(1.0f, 0.0f, 0.0f, 1.0f), // Red
    vec4(0.0f, 1.0f, 0.0f, 1.0f), // Green
    vec4(0.0f, 0.0f, 1.0f, 1.0f)  // Blue
};

layout(location = 0) out vec4 fragColor; // Output color to fragment shader

void main()
{
    gl_Position = vec4(vertices[gl_VertexIndex], 1.0);  // No transformation, direct position
    fragColor = colors[gl_VertexIndex];
}

// =================================================================================================
    
#type fragment

#version 450 core

layout(location = 0) in vec4 fragColor; // Input color from vertex shader

layout(location = 0) out vec4 outColor; // Output color to framebuffer

void main() 
{
    outColor = fragColor;  // Simple color output
}
