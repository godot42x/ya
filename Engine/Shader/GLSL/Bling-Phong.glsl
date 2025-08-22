#type vertex

#version 450 core

layout(location = 0) in vec3 aPos; 
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV; // aka aTexCoord
layout(location = 3) in vec3 aNormal; 

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
    fragNormal = normalize(  
        transpose(  
           inverse( mat3( uCamera.view* uCamera.model)) // inverse transpose matrix for normal transformation (when a face is not flat to us)
        ) 
        * aNormal);
}

// =================================================================================================

#next_shader_stage
#type fragment

#version 450 core

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPosition; // for lighting
layout(location = 3) in vec3 fragNormal; // for lighting

layout(location = 0) out vec4 outColor;

layout(set=2, binding=0) uniform sampler2D uTexture0; // see comment of SDL_CreateGPUShader, the set is the rule of SDL3!!!



layout(set = 3, binding = 0) uniform CameraBuffer{
    mat4 model;
    mat4 view;
    mat4 projection;
} uCamera;
layout(set = 3, binding = 1) uniform LightBuffer {
    vec3 lightDir; // Direction of light
} uLight; // for lighting

void main() 
{
    vec3 viewPos = -vec3( uCamera.view * vec4(0,0,0,1));
    vec3 viewDir = normalize(viewPos - fragPosition); // Direction from fragment to camera
    vec3 halfDir = normalize(viewDir + uLight.lightDir); // Halfway vector between light and view direction

    float shininess = 40;
    float specular = pow(max( 0, dot(fragNormal, halfDir)), shininess); 
    float diffuse  = max(dot(fragNormal, halfDir), 0.0); 
    float ambient = 0.3; 

    vec3 diffuseColor = vec3(1.0, 1.0, 1.0); 
    vec3 specularColor = vec3(1.0, 1.0, 1.0); 
    vec3 lighting = (ambient + diffuse) * diffuseColor  + specular* specularColor; 
    // lighting = unreal(lighting);


    outColor =  texture(uTexture0, fragUV) *
            fragColor * vec4(lighting, 1.0);
}
