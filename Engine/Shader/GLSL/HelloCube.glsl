#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aTexCoord;
layout(location = 2) in vec4 aNormal;


layout(push_constant) uniform PushConstants {
    mat4 model;
} PC;


layout(location = 0) out vec4 fragColor;

void main()
{
    // Create different positions for each instance
    vec3 instanceOffset = vec3(
        float(gl_InstanceIndex % 3) * 3.0 - 3.0,  // X: -3, 0, 3
        float(gl_InstanceIndex / 3) * 3.0 - 3.0,  // Y: -3, 0, 3  
        0.0
    );
    
    vec3 worldPos = aPos + instanceOffset;
    gl_Position = PC.model * vec4(worldPos, 1.0);
    
    // Color each instance differently based on instance index
    float hue = float(gl_InstanceIndex) / 9.0;
    fragColor = vec4(
        sin(hue * 6.28318) * 0.5 + 0.5,
        cos(hue * 6.28318) * 0.5 + 0.5,
        sin(hue * 3.14159) * 0.5 + 0.5,
        1.0
    );
}

////////////////////////
#type fragment
#version 450

layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main()
{
    // Output the color directly
    outColor = fragColor;
}