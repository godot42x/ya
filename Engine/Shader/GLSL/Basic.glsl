#type vertex

#version 450 core

layout(location = 0) in vec3 aPos; 
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV; // aka aTexCoord
layout(location = 3) in vec3 aNormal; 
layout(location = 4) in vec3 aTangent; 

// Camera/transform uniforms
layout(set = 1, binding = 0) uniform CameraBuffer {
    mat4 model;
    mat4 view;
    mat4 projection;
} uCamera;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragPosition; 
layout(location = 3) out vec3 fragNormal; 

void main()
{
    // Transform vertex position with view-projection matrix
    // mvp
    gl_Position = uCamera.projection * uCamera.view * uCamera.model * vec4(aPos, 1.0);
    fragColor = aColor;
    fragUV = aUV; 
    fragPosition =  vec3(uCamera.view * vec4(aPos,1.0)); // Pass the vertex position to the fragment shader
    fragNormal = normalize(aNormal);
}

// =================================================================================================

#type fragment

#version 450 core

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPosition; // for lighting
layout(location = 3) in vec3 fragNormal; // for lighting

layout(set=2, binding=0) uniform sampler2D uTexture0; // see comment of SDL_CreateGPUShader, the set is the rule of SDL3!!!

layout(set = 3, binding = 0) uniform CameraBuffer{
    mat4 model;
    mat4 view;
    mat4 projection;
} uCamera;

layout(set = 3, binding = 1) uniform LightBuffer {
    vec3 lightDir; 
    vec3 lightColor;
    float ambientIntensity;  
    float specularPower; 
} uLight; 


layout(location = 0) out vec4 outColor;

void main() 
{
    vec3 N = normalize(fragNormal); 
    vec3 L = normalize(- uLight.lightDir); // Light direction
    vec3 halfDir = normalize(L + fragPosition ); // Halfway vector between light and view direction

    float diffuse  = max(dot(N, L), 0.0);  // 漫反射
    float specular = pow(max( 0, dot(N, halfDir)), uLight.specularPower);  // 高光
    float ambient =  uLight.ambientIntensity * uLight.lightColor; // 环境光

    vec3 lighting = (ambient + diffuse) * texture(uTexture0, fragUV) 
                    +  specular * uLight.lightColor; 


    outColor =   vec4(lighting * fragColor.rgb, fragColor.a); 
            
}
