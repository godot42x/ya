#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec3 aNormal;

layout(set =0, binding =0, std140) uniform FrameUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint frameIdx;
    float time;
    vec3 cameraPos;  // 相机世界空间位置
} uFrame;

layout(set = 0, binding =3, std140) uniform DebugUBO {
    bool bDebugNormal;
    vec4 floatParam;
} uDebug;


layout(location = 0) out vec3 vPos;
layout(location = 1) out vec2 vTexcoord;
layout(location = 2) out vec3 vNormal;

layout(push_constant) uniform PushConstants{
    mat4 modelMat;
}pc;

#ifndef IS_VULKAN
#define IS_VULKAN 1
#endif

// MARK: Vertex Main
void main (){
    vec4 pos =  pc.modelMat * vec4(aPos, 1.0);
    vPos = pos.xyz;
    vTexcoord = aTexcoord;
    gl_Position = uFrame.projMat * uFrame.viewMat * pos;
    
    //生成一个 专门用于变换法向量的矩阵，确保法向量在经过模型矩阵的缩放、旋转等操作后，依然垂直于物体表面，从而保证光照计算的正确性。
    mat3 normalMatrix = transpose(inverse(mat3(pc.modelMat)));
    vec3 worldNormal = normalMatrix * aNormal;
    vNormal = worldNormal;
    
}


#type fragment
#version 450



layout(set =0, binding =0, std140) uniform FrameUBO {
    mat4 projMat;
    mat4 viewMat;
    ivec2 resolution;
    uint frameIdx;
    float time;
    vec3 cameraPos;  // 相机世界空间位置
} uFrame;


struct PointLight {
    vec3  position;      // 光源位置
    float intensity;    // 光照强度
    vec3  color;         // 光源颜色
    float radius;       // 光照范围（用于衰减计算）
};

#define MAX_POINT_LIGHTS 4

layout(set =0, binding =1, std140) uniform LightUBO {
    vec3  directionalLightDir;
    float directionalLightIntensity;

    vec3  directionalLightColor;
    float ambientIntensity;

    vec3  ambientColor;
    uint numPointLights;  // 实际使用的点光源数量

    PointLight pointLights[MAX_POINT_LIGHTS];
} uLit;


layout(set = 0, binding =2, std140) uniform DebugUBO {
    bool bDebugNormal;
    vec4 floatParam;
} uDebug;


layout(set =1, binding = 0) uniform sampler2D uTexDiffuse;
layout(set =1, binding = 1) uniform sampler2D uTexSpecular;


layout(set = 2, binding = 0) uniform ParamUBO {
    vec3 ambient; 
    vec3 diffuse;
    vec3 specular;
    float shininess;
} uParams;


layout(location = 0) in vec3 vPos;
layout(location = 1) in vec2 vTexcoord;
layout(location = 2) in vec3 vNormal;

layout(location = 0) out vec4 fColor;

// 计算点光源的衰减
float calculateAttenuation(float distance, float radius) {
    // 使用物理衰减：1 / (distance^2)
    return 1;
    // 添加半径限制，超出范围衰减为 0
    if (distance > radius) {
        return 0.0;
    }
    float attenuation = 1.0 / (1.0 + distance * distance);
    // 平滑过渡到边缘
    float smoothFactor = 1.0 - pow(distance / radius, 4.0);
    return attenuation * smoothFactor;
}



// MARK: Fragment Main
void main ()
{
    vec3 norm = normalize(vNormal);
    // from fragment to camera(eye)
    vec3 viewDir = normalize(uFrame.cameraPos - vPos);
    float shininess = uDebug.floatParam.x == 0.0 ? uParams.shininess : uDebug.floatParam.x;
    
    if(uDebug.bDebugNormal){
        fColor = vec4(norm * 0.5 + 0.5, 1.0);
        return;
    }
    if(uLit.numPointLights == 0){
        fColor = vec4(0,0,1,1);
        return;
    }

    vec4 diffuseTexColor = texture(uTexDiffuse, vTexcoord);
    vec4 specularTexColor = texture(uTexSpecular, vTexcoord);
    // lib: 尝试在片段着色器中反转镜面光贴图的颜色值，让木头显示镜面高光而钢制边缘不反光（由于钢制边缘中有一些裂缝，边缘仍会显示一些镜面高光，虽然强度会小很多
    // specularTexColor  = vec4(1.0) -specularTexColor;
    
    
    // 累积所有点光源的光照
    vec3 lighting = vec3(0.0);
    
    for (uint i = 0u; i < uLit.numPointLights && i < MAX_POINT_LIGHTS; ++i) {

        PointLight light = uLit.pointLights[i];

        // 修正：lightDir 应该是从片段指向光源的方向
        vec3 lightDir  = normalize(light.position - vPos);
        float distance = length(light.position - vPos);
        vec3 lampColor = light.color * light.intensity;
        
        // 计算衰减
        float attenuation = calculateAttenuation(distance, light.radius);
        if (attenuation <= 0.0) {
            continue; // 超出光照范围，跳过
        }

        // 环境光/ambient（环境光不受距离衰减影响，但强度应该较小）
        vec3 ambient = lampColor * uParams.ambient * diffuseTexColor.rgb * 0.1;
        
        // 漫反射/diffuse
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = lampColor* attenuation*   (diff * uParams.diffuse) * diffuseTexColor.rgb;
        
        // 高光/specular
    #if BLING_PHONG_HALFWAY
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    #else
        // 修正：使用正确的反射方向（反射入射光线）
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    #endif
        vec3 specular = lampColor * attenuation * spec * uParams.specular * specularTexColor.rgb;
        
        // 累加光照
        lighting += (ambient + diffuse + specular);
    }
    
    // 限制输出范围，避免过曝导致的视觉错误
    fColor = vec4(clamp(lighting, 0.0, 1.0), 1.0);
}